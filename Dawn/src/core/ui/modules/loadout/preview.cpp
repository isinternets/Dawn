// SPDX-License-Identifier: GPL-3.0-only
// Icon layer layouts adapted from Sundial by KyleThmpsn; see vendor/sundial/NOTICE.md.
#include "preview.h"
#include <Windows.h>
#include "client/content/items/packages/internal.h"
#include "client/hooks/bootflow/bc7_codec.h"
#include "core/filesystem/path.h"
#include "state/editor/localized_strings.h"
#include <d3d11.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dawn::core::ui::modules::loadout::preview {
namespace {
namespace packages = client::content::items::packages;
namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;
namespace strings = state::editor::strings;
namespace bc7 = client::hooks::bootflow::bc7;
// Where each layer sits in an icon container, in the order they are drawn, and what each one is.
constexpr std::array<std::size_t, 4> kLayerOffsets{0x1C, 0x14, 0x20, 0x24};
constexpr std::array<const char*, 4> kLayerRoles{"Background", "Icon", "Watermark", "Foreground"};
constexpr std::uint8_t kPrimaryRole = 1;
// One layer: its texture, and the image record it was read from with its place in the stack.
struct Layer { unsigned width{}, height{}, format{}, pitch{}; std::vector<std::byte> data; std::uint32_t tag{}; std::uint8_t role{}; };
// The primary layer's size is the icon's own: every other layer is drawn over the same box.
struct Result { std::uint32_t tag{}; std::vector<Layer> layers; bool primary{}; unsigned width{}, height{}; };
struct Texture { std::array<ID3D11ShaderResourceView*, 4> views{}; std::array<LayerDetails, 4> facts{}; std::size_t count{}; int lastFrame{}; bool pending{}, failed{}; unsigned width{}, height{}; };
// One export asked for: the icon, and the one layer of it to save, or zero for the whole stack.
struct ExportRequest { std::uint32_t tag{}; std::uint32_t layer{}; };
// The last export the worker finished, which the page takes once.
struct Exported { std::uint32_t tag{}; bool saved{}, ready{}; std::string message; };
ID3D11Device* g_device{};
std::unordered_map<std::uint32_t, Texture> g_textures;
std::mutex g_lock;
std::condition_variable g_wake;
std::deque<std::uint32_t> g_requests;
std::deque<Result> g_results;
std::deque<ExportRequest> g_exports;
Exported g_exported;
// Class of a bare image record, which the interface packages keep their art in; zero until known.
std::atomic<std::uint32_t> g_imageClass{};
// Dawn skips its teardown when a hook cannot come off, and a joinable std::thread destroyed at exit
// calls std::terminate, so a worker `shutdown` never joined is detached instead.
struct Worker { std::thread thread; ~Worker() { if (thread.joinable()) thread.detach(); } };
Worker g_worker;
bool g_stop{};
bool valid_tag(std::uint32_t tag) { return tables::package_of(tag) != tables::kAbsentPackageId; }
// Reads one image record, which is one layer of an icon, into its texture.
bool image(const reader::Source& source, reader::Scratch& scratch, const std::vector<std::byte>& definition, Layer& out) {
    std::uint32_t textureTag{}, dataTag{}; std::size_t resource{};
    tables::Array lanes{}, textures{};
    std::vector<std::byte> header;
    if (!strings::relative(definition, 0x10, resource)
        || !tables::find_array_at(definition, resource, lanes) || lanes.count > 32
        || !tables::find_array_at(definition, lanes.dataOffset, textures) || textures.count > 32
        || !strings::read(std::span<const std::byte>(definition), textures.dataOffset, textureTag)
        || !reader::read_tag(source, scratch, textureTag, header, dataTag) || !valid_tag(dataTag)
        || !reader::read_tag(source, scratch, dataTag, out.data)) return false;
    std::uint16_t width{}, height{};
    if (!strings::read(std::span<const std::byte>(header), 4, out.format)
        || !strings::read(std::span<const std::byte>(header), 0x0E, width)
        || !strings::read(std::span<const std::byte>(header), 0x10, height)
        || !width || !height || width > 2048 || height > 2048) return false;
    out.width = width; out.height = height;
    unsigned rows = height;
    switch (out.format) {
    case 28: case 29: case 87: case 91: out.pitch = width * 4U; break;
    case 71: case 72: out.pitch = ((width + 3U) / 4U) * 8U; rows = (height + 3U) / 4U; break;
    case 74: case 75: case 77: case 78: case 98: case 99:
        out.pitch = ((width + 3U) / 4U) * 16U; rows = (height + 3U) / 4U; break;
    default: return false;
    }
    const auto length = static_cast<std::size_t>(out.pitch) * rows;
    if (out.data.size() < length) return false;
    out.data.resize(length);
    return true;
}
Result read_icon(std::uint32_t tag, reader::Scratch& scratch) {
    Result result; result.tag = tag;
    reader::BlockKeys keys{};
    struct CleanKeys { reader::BlockKeys& keys; ~CleanKeys() { SecureZeroMemory(&keys, sizeof keys); } } clean{keys};
    core::path::Buffer path{};
    if (!packages::package_directory(path) || !packages::collect_keys(keys)) return result;
    reader::Source source{path.chars.data(), &keys};
    std::vector<std::byte> container;
    std::uint32_t classId{};
    if (!reader::read_tag(source, scratch, tag, container, classId)) return result;
    const auto keep = [&result](Layer&& decoded) {
        if (decoded.role == kPrimaryRole) { result.primary = true; result.width = decoded.width; result.height = decoded.height; }
        result.layers.push_back(std::move(decoded));
    };
    // The interface packages keep their art bare: the record is the image itself, with no container.
    if (classId != 0 && classId == g_imageClass.load(std::memory_order_relaxed)) {
        Layer decoded;
        if (image(source, scratch, container, decoded)) { decoded.tag = tag; decoded.role = kPrimaryRole; keep(std::move(decoded)); }
        return result;
    }
    for (std::size_t i = 0; i < kLayerOffsets.size(); ++i) {
        Layer decoded;
        std::vector<std::byte> definition;
        if (strings::read(std::span<const std::byte>(container), kLayerOffsets[i], decoded.tag) && valid_tag(decoded.tag)
            && reader::read_tag(source, scratch, decoded.tag, definition) && image(source, scratch, definition, decoded)) {
            decoded.role = static_cast<std::uint8_t>(i);
            keep(std::move(decoded));
        }
    }
    return result;
}

// Export: every layer decoded to straight-alpha RGBA8, stacked as the draw list blends them.
struct Image { unsigned width{}, height{}; std::vector<std::uint8_t> rgba; };
void unpack565(unsigned value, std::uint8_t* rgb) {
    rgb[0] = static_cast<std::uint8_t>(((value >> 11U) & 31U) * 255U / 31U);
    rgb[1] = static_cast<std::uint8_t>(((value >> 5U) & 63U) * 255U / 63U);
    rgb[2] = static_cast<std::uint8_t>((value & 31U) * 255U / 31U);
}
// The colour half of a BC1, BC2 or BC3 block. Only BC1 has the three-colour mode with a clear texel.
void decode_color(const std::uint8_t* block, bool punchThrough, std::uint8_t* rgba) {
    const unsigned c0 = block[0] | (static_cast<unsigned>(block[1]) << 8U), c1 = block[2] | (static_cast<unsigned>(block[3]) << 8U);
    std::uint8_t palette[4][4]{};
    unpack565(c0, palette[0]); unpack565(c1, palette[1]);
    const bool four = !punchThrough || c0 > c1;
    for (int channel = 0; channel < 3; ++channel) {
        const unsigned a = palette[0][channel], b = palette[1][channel];
        palette[2][channel] = static_cast<std::uint8_t>(four ? (2U * a + b) / 3U : (a + b) / 2U);
        palette[3][channel] = static_cast<std::uint8_t>(four ? (a + 2U * b) / 3U : 0U);
    }
    palette[0][3] = palette[1][3] = palette[2][3] = 255; palette[3][3] = static_cast<std::uint8_t>(four ? 255U : 0U);
    const std::uint32_t indices = block[4] | (static_cast<std::uint32_t>(block[5]) << 8U)
        | (static_cast<std::uint32_t>(block[6]) << 16U) | (static_cast<std::uint32_t>(block[7]) << 24U);
    for (unsigned texel = 0; texel < 16; ++texel) std::memcpy(rgba + texel * 4U, palette[(indices >> (texel * 2U)) & 3U], 4);
}
// BC3's interpolated alpha block.
void decode_alpha(const std::uint8_t* block, std::uint8_t* rgba) {
    std::uint8_t values[8]{block[0], block[1]};
    const unsigned a = block[0], b = block[1];
    if (a > b) for (unsigned i = 1; i < 7; ++i) values[i + 1] = static_cast<std::uint8_t>(((7U - i) * a + i * b) / 7U);
    else { for (unsigned i = 1; i < 5; ++i) values[i + 1] = static_cast<std::uint8_t>(((5U - i) * a + i * b) / 5U); values[6] = 0; values[7] = 255; }
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(block[2 + i]) << (8U * i);
    for (unsigned texel = 0; texel < 16; ++texel) rgba[texel * 4U + 3U] = values[(bits >> (3U * texel)) & 7U];
}
bool decode(const Layer& source, Image& image) {
    image.width = source.width; image.height = source.height;
    image.rgba.assign(static_cast<std::size_t>(source.width) * source.height * 4U, 0);
    const auto* data = reinterpret_cast<const std::uint8_t*>(source.data.data());
    if (source.format == 28 || source.format == 29 || source.format == 87 || source.format == 91) {
        const bool bgra = source.format == 87 || source.format == 91;
        for (unsigned y = 0; y < source.height; ++y) for (unsigned x = 0; x < source.width; ++x) {
            const std::uint8_t* in = data + static_cast<std::size_t>(y) * source.pitch + x * 4U;
            std::uint8_t* out = image.rgba.data() + (static_cast<std::size_t>(y) * source.width + x) * 4U;
            out[0] = in[bgra ? 2 : 0]; out[1] = in[1]; out[2] = in[bgra ? 0 : 2]; out[3] = in[3];
        }
        return true;
    }
    const bool bc1 = source.format == 71 || source.format == 72, bc2 = source.format == 74 || source.format == 75;
    const bool bc7 = source.format == 98 || source.format == 99;
    const std::size_t blockBytes = bc1 ? 8U : 16U;
    std::uint8_t texels[bc7::kBlockTexelBytes]{};
    for (unsigned by = 0; by < (source.height + 3U) / 4U; ++by) for (unsigned bx = 0; bx < (source.width + 3U) / 4U; ++bx) {
        const std::uint8_t* block = data + static_cast<std::size_t>(by) * source.pitch + bx * blockBytes;
        if (bc7) bc7::decode_block(block, texels);
        else if (bc1) decode_color(block, true, texels);
        else {
            decode_color(block + 8, false, texels);
            if (bc2) for (unsigned texel = 0; texel < 16; ++texel)
                texels[texel * 4U + 3U] = static_cast<std::uint8_t>(((block[texel / 2U] >> ((texel & 1U) * 4U)) & 15U) * 17U);
            else decode_alpha(block, texels);
        }
        for (unsigned texel = 0; texel < 16; ++texel) {
            const unsigned x = bx * 4U + (texel & 3U), y = by * 4U + (texel >> 2U);
            if (x < source.width && y < source.height)
                std::memcpy(image.rgba.data() + (static_cast<std::size_t>(y) * source.width + x) * 4U, texels + texel * 4U, 4);
        }
    }
    return true;
}
// Lays one layer over what is below it, stretched to the canvas as the draw list stretches it.
void blend(Image& canvas, const Image& layer) {
    for (unsigned y = 0; y < canvas.height; ++y) for (unsigned x = 0; x < canvas.width; ++x) {
        const unsigned sx = x * layer.width / canvas.width, sy = y * layer.height / canvas.height;
        const std::uint8_t* s = layer.rgba.data() + (static_cast<std::size_t>(sy) * layer.width + sx) * 4U;
        std::uint8_t* d = canvas.rgba.data() + (static_cast<std::size_t>(y) * canvas.width + x) * 4U;
        const float sa = static_cast<float>(s[3]) / 255.0F, da = static_cast<float>(d[3]) / 255.0F;
        const float out = sa + (da * (1.0F - sa));
        if (out <= 0.0F) { std::memset(d, 0, 4); continue; }
        for (int c = 0; c < 3; ++c)
            d[c] = static_cast<std::uint8_t>(std::lround(((s[c] * sa) + (d[c] * da * (1.0F - sa))) / out));
        d[3] = static_cast<std::uint8_t>(std::lround(out * 255.0F));
    }
}
template <typename Interface> void release_com(Interface*& object) { if (object) { object->Release(); object = nullptr; } }
// WIC's PNG encoder takes BGRA on every Windows it runs on, so the image is swizzled for it.
bool write_png(const wchar_t* path, const Image& image) {
    std::vector<std::uint8_t> bgra(image.rgba);
    for (std::size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    IWICImagingFactory* factory{}; IWICStream* stream{}; IWICBitmapEncoder* encoder{}; IWICBitmapFrameEncode* frame{};
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    const bool written = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr))
        && SUCCEEDED(frame->SetSize(image.width, image.height)) && SUCCEEDED(frame->SetPixelFormat(&format))
        && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)
        && SUCCEEDED(frame->WritePixels(image.height, image.width * 4U, static_cast<UINT>(bgra.size()), bgra.data()))
        && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    release_com(frame); release_com(encoder); release_com(stream); release_com(factory);
    return written;
}
bool make_folder(const wchar_t* relative) {
    core::path::Buffer folder{};
    return core::path::artifact_file(relative, folder)
        && (CreateDirectoryW(folder.chars.data(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS);
}
// Reads one icon again, stacks its layers and saves it, or saves the one layer asked for at its own
// size, named by that layer's tag. Runs on the worker; the page shows `message`.
bool export_icon(const ExportRequest& request, reader::Scratch& scratch, std::string& message) {
    const Result icon = read_icon(request.tag, scratch);
    const Layer* only = nullptr;
    for (const Layer& part : icon.layers) if (request.layer != 0 && part.tag == request.layer) only = &part;
    if (request.layer != 0 && only == nullptr) { message = "This layer has no artwork to export."; return false; }
    if (only == nullptr && (!icon.primary || icon.width == 0 || icon.height == 0)) { message = "This icon has no artwork to export."; return false; }
    Image canvas; canvas.width = only != nullptr ? only->width : icon.width; canvas.height = only != nullptr ? only->height : icon.height;
    canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * canvas.height * 4U, 0);
    for (const Layer& part : icon.layers) {
        Image decoded;
        if ((only == nullptr || &part == only) && decode(part, decoded)) blend(canvas, decoded);
    }
    const std::uint32_t named = only != nullptr ? only->tag : request.tag;
    wchar_t relative[64]{};
    (void)std::swprintf(relative, std::size(relative), L"exports\\icons\\0x%08X.png", named);
    core::path::Buffer file{};
    if (!make_folder(L"exports") || !make_folder(L"exports\\icons") || !core::path::artifact_file(relative, file)
        || !write_png(file.chars.data(), canvas)) { message = "Couldn't write the PNG to the Dawn folder."; return false; }
    char shown[96]{};
    (void)std::snprintf(shown, sizeof shown, "Saved to Dawn\\exports\\icons\\0x%08X.png", named);
    message = shown;
    return true;
}

void run() noexcept {
    try {
        // The PNG encoder is a COM object, and this worker is the thread that makes it.
        struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } com{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        auto scratch = std::make_unique<reader::Scratch>();
        struct CloseFiles { reader::Scratch& scratch; ~CloseFiles() { reader::close_files(scratch); } } close{*scratch};
        for (;;) {
            std::uint32_t tag{};
            ExportRequest request;
            bool exporting = false;
            { std::unique_lock lock(g_lock);
                g_wake.wait(lock, [] { return g_stop || !g_requests.empty() || !g_exports.empty(); });
                if (g_stop) break;
                exporting = !g_exports.empty();
                if (exporting) { request = g_exports.front(); g_exports.pop_front(); }
                // Newest first: after a fast scroll the newest asks are the icons on screen now.
                else { tag = g_requests.back(); g_requests.pop_back(); }
            }
            if (exporting) {
                Exported done{request.tag, false, true, {}};
                try { done.saved = export_icon(request, *scratch, done.message); } catch (...) { done.message = "The export failed."; }
                std::lock_guard lock(g_lock); g_exported = std::move(done);
                continue;
            }
            Result result; result.tag = tag;
            try { result = read_icon(tag, *scratch); } catch (...) { /* Return a failed preview for this item. */ }
            { std::unique_lock lock(g_lock);
                g_wake.wait(lock, [] { return g_stop || g_results.size() < 48; });
                if (g_stop) break;
                g_results.push_back(std::move(result));
            }
        }
    } catch (...) {
        std::lock_guard lock(g_lock); g_stop = true; g_requests.clear(); g_exports.clear();
    }
}
void free(Texture& texture) { for (auto*& view : texture.views) if (view) { view->Release(); view = nullptr; } }
void drain() {
    std::deque<Result> results;
    { std::lock_guard lock(g_lock); results.swap(g_results); }
    g_wake.notify_all();
    for (auto& result : results) {
        auto it = g_textures.find(result.tag);
        if (it == g_textures.end() || !g_device) continue;
        auto& texture = it->second; texture.pending = false;
        if (!result.primary) { texture.failed = true; continue; }
        // A device reset can drop an in-flight read and let the same tag be requested again, so a
        // second result can arrive for a tag that already has its views. Those views may be in this
        // frame's draw list already, and freeing them left the renderer drawing released textures;
        // the second result is the same artwork, so it is dropped instead.
        if (texture.count != 0) continue;
        texture.width = result.width; texture.height = result.height;
        for (const auto& data : result.layers) {
            if (texture.count >= texture.views.size()) break;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = data.width; desc.Height = data.height; desc.MipLevels = 1; desc.ArraySize = 1;
            desc.Format = static_cast<DXGI_FORMAT>(data.format); desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA pixels{data.data.data(), data.pitch, 0};
            ID3D11Texture2D* image{}; ID3D11ShaderResourceView* view{};
            if (SUCCEEDED(g_device->CreateTexture2D(&desc, &pixels, &image))) {
                const auto created = g_device->CreateShaderResourceView(image, nullptr, &view);
                image->Release();
                if (SUCCEEDED(created)) {
                    texture.facts[texture.count] = {kLayerRoles[data.role], data.tag, data.width, data.height};
                    texture.views[texture.count++] = view;
                }
            }
        }
        texture.failed = texture.count == 0;
    }
}
void ensure_worker() { if (!g_worker.thread.joinable()) { { std::lock_guard lock(g_lock); g_stop = false; } g_worker.thread = std::thread(run); } }
// Looks one icon up, asking the worker for it the first time. Null until its artwork is in.
const Texture* acquire(std::uint32_t tag) {
    drain();
    ensure_worker();
    const int frame = ImGui::GetFrameCount();
    if (!g_textures.contains(tag) && g_textures.size() >= 256) {
        auto oldest = g_textures.end();
        for (auto it = g_textures.begin(); it != g_textures.end(); ++it)
            if (!it->second.pending && it->second.lastFrame < frame - 1
                && (oldest == g_textures.end() || it->second.lastFrame < oldest->second.lastFrame)) oldest = it;
        if (oldest == g_textures.end()) return nullptr;
        free(oldest->second); g_textures.erase(oldest);
    }
    auto [it, added] = g_textures.try_emplace(tag);
    auto& texture = it->second; texture.lastFrame = frame;
    if (added) {
        std::lock_guard lock(g_lock);
        if (g_stop) { g_textures.erase(it); return nullptr; }
        // A full queue lets go of its oldest ask, which a long list has most likely scrolled past,
        // rather than turn away the icon on screen. A dropped icon is asked for again if it returns.
        if (g_requests.size() >= 128) {
            const auto stale = g_textures.find(g_requests.front());
            g_requests.pop_front();
            if (stale != g_textures.end() && stale->second.pending) g_textures.erase(stale);
        }
        g_requests.push_back(tag); texture.pending = true; g_wake.notify_one();
    }
    return texture.count != 0 ? &texture : nullptr;
}
// Draws every layer over one box, or only the one at `layer` when it is a layer the icon has.
void paint(const Texture& texture, ImVec2 min, ImVec2 max, ImU32 tint, int layer = -1) {
    for (std::size_t i = 0; i < texture.count; ++i)
        if (layer < 0 || static_cast<std::size_t>(layer) == i)
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture.views[i]), min, max, {0.0F, 0.0F}, {1.0F, 1.0F}, tint);
}
}
void set_image_class(std::uint32_t classId) noexcept { g_imageClass.store(classId, std::memory_order_relaxed); }
void attach(ID3D11Device* device) noexcept { release(); g_device = device; }
bool unavailable(std::uint32_t tag) noexcept {
    const auto it = g_textures.find(tag);
    if (it != g_textures.end() && it->second.failed) return true;
    std::lock_guard lock(g_lock); return g_stop;
}
void release() noexcept {
    for (auto& [tag, texture] : g_textures) { (void)tag; free(texture); }
    g_textures.clear(); g_device = nullptr;
    std::lock_guard lock(g_lock); g_requests.clear(); g_results.clear();
    g_wake.notify_all();
}
void shutdown() noexcept {
    { std::lock_guard lock(g_lock); g_stop = true; }
    g_wake.notify_all();
    if (g_worker.thread.joinable()) g_worker.thread.join();
    { std::lock_guard lock(g_lock); g_requests.clear(); g_results.clear(); g_exports.clear(); }
}
bool draw(std::uint32_t tag, ImVec2 position, float size, ImU32 tint) noexcept {
    if (!g_device || !valid_tag(tag)) return false;
    try {
        const Texture* texture = acquire(tag);
        if (texture == nullptr) return false;
        paint(*texture, position, {position.x + size, position.y + size}, tint);
        return true;
    } catch (...) { return false; }
}
bool draw_fitted(std::uint32_t tag, ImVec2 origin, ImVec2 box, float texel, float largest, ImU32 tint, int layer) noexcept {
    if (!g_device || !valid_tag(tag)) return false;
    try {
        const Texture* texture = acquire(tag);
        if (texture == nullptr || texture->width == 0 || texture->height == 0) return false;
        // One layer alone is fitted at its own size, which need not be the icon's.
        const bool one = layer >= 0 && static_cast<std::size_t>(layer) < texture->count;
        const unsigned texelsWide = one ? texture->facts[static_cast<std::size_t>(layer)].width : texture->width;
        const unsigned texelsHigh = one ? texture->facts[static_cast<std::size_t>(layer)].height : texture->height;
        const float width = static_cast<float>(texelsWide) * texel, height = static_cast<float>(texelsHigh) * texel;
        const float scale = (std::min)({box.x / width, box.y / height, largest});
        const ImVec2 size{width * scale, height * scale};
        const ImVec2 min{std::floor(origin.x + ((box.x - size.x) * 0.5F)), std::floor(origin.y + ((box.y - size.y) * 0.5F))};
        paint(*texture, min, {min.x + size.x, min.y + size.y}, tint, one ? layer : -1);
        return true;
    } catch (...) { return false; }
}
bool details(std::uint32_t tag, Details& output) noexcept {
    const auto it = g_textures.find(tag);
    if (it == g_textures.end() || it->second.count == 0) return false;
    output = {it->second.width, it->second.height, static_cast<unsigned>(it->second.count)};
    return true;
}
bool layer_details(std::uint32_t tag, unsigned index, LayerDetails& output) noexcept {
    const auto it = g_textures.find(tag);
    if (it == g_textures.end() || index >= it->second.count) return false;
    output = it->second.facts[index];
    return true;
}
bool request_export(std::uint32_t tag, std::uint32_t layer) noexcept {
    if (!valid_tag(tag)) return false;
    try {
        ensure_worker();
        std::lock_guard lock(g_lock);
        if (g_stop || g_exports.size() >= 8) return false;
        g_exports.push_back({tag, layer});
        g_wake.notify_one();
        return true;
    } catch (...) { return false; }
}
bool take_export(std::uint32_t& tag, bool& saved, std::string& message) noexcept {
    try {
        std::lock_guard lock(g_lock);
        if (!g_exported.ready) return false;
        tag = g_exported.tag; saved = g_exported.saved; message = std::move(g_exported.message);
        g_exported = {};
        return true;
    } catch (...) { return false; }
}
}
