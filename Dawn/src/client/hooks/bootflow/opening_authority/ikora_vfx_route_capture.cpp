#include "client/hooks/bootflow/opening_authority/ikora_vfx_route_capture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

namespace dawn::client::hooks::bootflow::opening_authority::ikora_vfx_route {
namespace {

[[nodiscard]] consteval std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    return static_cast<std::uint8_t>(value - 'a' + 10);
}

template <std::size_t N>
[[nodiscard]] consteval auto hex_bytes(const char (&text)[N]) {
    static_assert((N - 1U) % 2U == 0U);
    std::array<std::byte, (N - 1U) / 2U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto high = hex_nibble(text[index * 2U]);
        const auto low = hex_nibble(text[index * 2U + 1U]);
        bytes[index] = std::byte{static_cast<std::uint8_t>((high << 4U) | low)};
    }
    return bytes;
}

constexpr auto kPrefix588690 = hex_bytes("48895C2410574883EC208B592C488BF98B5124488D4C2430E8239ADAFF488B05");
constexpr auto kPrefix58B9A0 = hex_bytes("40574883EC40488BF980FAFF0F842F01000048895C245033DB4584C075364584");
constexpr auto kPrefix5902C0 = hex_bytes("4C8BDC555641554157498DABE8F7FFFF4881ECF8080000488B05AA97B1014833");
constexpr auto kPrefix56D9B0 = hex_bytes("48895C241848896C2420565741564881EC40010000488B05BCC0B3014833C448");
constexpr auto kPrefixB31910 = hex_bytes("488B11488B4908488B421848FF640230");
constexpr auto kPrefix58F810 = hex_bytes("48895C241048896C2418565741564881EC800000000F29742470498BE9410F10");
constexpr auto kPrefix58FA20 = hex_bytes("48895C240848896C2410488974241848897C242041564883EC2033DB4D8BF149");
constexpr auto kPrefix58EB40 = hex_bytes("405553565741564157488DAC24E8D7FFFFB818290000E8D5DF2E01482BE00F29");
constexpr auto kPrefix12065D0 = hex_bytes("48895C2408488974241848897C2420554154415541564157488D6C24D04881EC");
constexpr auto kPrefix11E83A0 = hex_bytes("4C8BDC49895B08498973104D894B20574883EC30488B05CD16EC004833C44889");
constexpr auto kPrefix1212AF0 = hex_bytes("448944241855415541564881EC900000004C8B59404C8BEA4D85DB0F297C2450");
constexpr auto kPrefix120B4F0 = hex_bytes("48895C2408574883EC208BC18BD9C1F80D8BFB8BC881E7FF1F00004881C90000FC0F0FB7C048C1E9124823C8488B054DE7220148C1E1064803080FAF79304863");
constexpr auto kPrefix120B3D0 = hex_bytes("48895C2408574883EC208BC18BD9C1F80D8BFB8BC881E7FF1F00004881C90000FC0F0FB7C048C1E9124823C8488B056DE8220148C1E1064803080FAF79304863");
constexpr auto kPrefix12103E0 = hex_bytes("488BC4488958185741544155415641574881EC00010000440F2940A8440F2950");
constexpr auto kPrefixA1F360 = hex_bytes("48895C241048896C241848897C242041564883EC30817910EDFE0DF0498BE949");
constexpr auto kPrefixA1F640 = hex_bytes("895424105355564154415541574881EC88000000488B417033F64D8BE94D8BF8");
constexpr auto kPrefixA1E8A0 = hex_bytes("48895C240848896C2410488974241848897C24204154415641574883EC60488B");
constexpr auto kPrefixB314A0 = hex_bytes("4C8B11488B4908498B42184AFF641048");
constexpr auto kPrefix11F0510 = hex_bytes("405355565741564883EC500F29742440488B056195EB004833C4488944243083");

struct ManifestEntry final {
    NativeSurface surface;
    std::uint32_t rva;
    std::span<const std::byte> prefix;
    std::uint32_t bytes;
    Sha256 sha;
};

constexpr std::array<ManifestEntry, kNativeSurfaceCount> kManifest{{
    {NativeSurface::actor_terminal_invalidation, 0x588690U, kPrefix588690, 168U,
     hex_bytes("4A811768F270AA642D449301639FF484A6991A2A0E06B6D2FF2F262605998EFB")},
    {NativeSurface::transition_wrapper, 0x58B9A0U, kPrefix58B9A0, 432U,
     hex_bytes("8193409DCDE25B37FFDB3D1007CE716FE2610F5A509031EDEA81DCBFC9E77ECD")},
    {NativeSurface::scene_actor_scheduler, 0x5902C0U, kPrefix5902C0, 744U,
     hex_bytes("194F23AF75297503D954A21CBD627187C294C44942F7F37F211A2EEA5ABAE595")},
    {NativeSurface::entity_factory, 0x56D9B0U, kPrefix56D9B0, 1096U,
     hex_bytes("61FD5D85E9BB99D1E5C976C74F659406B9CCF605298AA8C796E07B1C193D2093")},
    {NativeSurface::component_tail_dispatch, 0xB31910U, kPrefixB31910, 16U,
     hex_bytes("FAEC892BCBD9722F8194A160FE84BDDE2E5BB5BE893DD75E4F4E649506ECF44E")},
    {NativeSurface::cache_rebuild, 0x58F810U, kPrefix58F810, 522U,
     hex_bytes("DB8B51921CBD82F35855D8EA0DAC52C51D5DCC87C349BA882F99BC2C0F0BF7AE")},
    {NativeSurface::descriptor_walk, 0x58FA20U, kPrefix58FA20, 192U,
     hex_bytes("8F2764C95CA24CB9DF2D23D2DBFF42D149EE4E3A903771CB0516722281530FDD")},
    {NativeSurface::source_writer, 0x58EB40U, kPrefix58EB40, 3174U,
     hex_bytes("0DB8BAF3461BC23628B16A96B09649A211F41F8E0FDE7550D21A259264941376")},
    {NativeSurface::effect_create_one, 0x12065D0U, kPrefix12065D0, 2352U,
     hex_bytes("6F121FFC27CE304F70870FE27E8D7AF9D41354D05587FA8B51164BD3C0E04B06")},
    {NativeSurface::effect_object_initializer, 0x11E83A0U, kPrefix11E83A0, 712U,
     hex_bytes("E670A4EB96C80A8966B3335743058E5D93096C7AD14602D9471F32A3B4A1EB47")},
    {NativeSurface::effect_group_create, 0x1212AF0U, kPrefix1212AF0, 906U,
     hex_bytes("854C39EF0AEED2C91EFD3CB8E611F70F9A1BB8926DFD97DD2DB7921D187505B0")},
    {NativeSurface::effect_explicit_destroy, 0x120B4F0U, kPrefix120B4F0, 147U,
     hex_bytes("CBA62A3C50F73D471F8EF7E95D8400C005260358EE1E23408A438C69A7C3E1EB")},
    {NativeSurface::effect_priority_eviction_destroy, 0x120B3D0U, kPrefix120B3D0, 117U,
     hex_bytes("9347E9FA1781E0E92DD77DFB433FA83F735A0C9BF2BDB9D09ABFB7BC3405BE73")},
    {NativeSurface::effect_update_and_expiry, 0x12103E0U, kPrefix12103E0, 1618U,
     hex_bytes("EDEF643469C2AEBD9F9EA11614AA09AFC74EF5ADA96F2F7B7F3F7726FA3BFE81")},
    {NativeSurface::kind2_runtime_resolver, 0xA1F360U, kPrefixA1F360, 348U,
     hex_bytes("5606BAC3B40750E3CDD4366590973525AB592DB1F36DE03BFDF76FD6DAA56505")},
    {NativeSurface::provider_selection, 0xA1F640U, kPrefixA1F640, 641U,
     hex_bytes("BA34D019F279C18C73467CE4737E131D58609B5A1531632DE1B2704E7977238A")},
    {NativeSurface::candidate_socket_resolver, 0xA1E8A0U, kPrefixA1E8A0, 465U,
     hex_bytes("34DFD7332C316933C82994C9B128BCC466DB500F357A875D219CF1BDCDBA4BA2")},
    {NativeSurface::pose_socket_dispatch, 0xB314A0U, kPrefixB314A0, 16U,
     hex_bytes("97B4D5379C8241C1C78CC436802C8C1D59AEA523EE0C5264DB54405E3C083747")},
    {NativeSurface::downstream_bounds_oracle, 0x11F0510U, kPrefix11F0510, 913U,
     hex_bytes("6EDDB4B9871882411B959528313444BE52A1D138923A630DB5EB61E83DD6EC38")},
}};

constexpr std::array<DirectEdge, 33U> kDirectEdges{{
    {0x58F96CU, 0x58FA20U, 0xE8U}, {0x58FAB5U, 0x58EB40U, 0xE8U},
    {0x58ECE6U, 0xA1F360U, 0xE8U}, {0x58EDD5U, 0xA1F360U, 0xE8U},
    {0x58EEEFU, 0xA1F360U, 0xE8U}, {0x58F00DU, 0xA1F360U, 0xE8U},
    {0xA1F47CU, 0xA1F640U, 0xE8U}, {0xA1F812U, 0xA1E8A0U, 0xE8U},
    {0xA1F865U, 0xA1E8A0U, 0xE8U}, {0xA1E967U, 0xB314A0U, 0xE8U},
    {0x58BB4BU, 0x588690U, 0xE9U}, {0x59046FU, 0x56D990U, 0xE8U},
    {0x56D9A1U, 0x56D9B0U, 0xE8U}, {0x1206E14U, 0x11F0510U, 0xE8U},
    {0x121074FU, 0x11F0510U, 0xE8U}, {0x1206E89U, 0x120B4F0U, 0xE8U},
    {0x1212CB0U, 0x12065D0U, 0xE8U}, {0x1212DF7U, 0x12065D0U, 0xE8U},
    {0x120B540U, 0x11E3520U, 0xE8U}, {0x120B54EU, 0x1270410U, 0xE8U},
    {0x120B558U, 0x1212070U, 0xE8U}, {0x120B573U, 0x122C1A0U, 0xE8U},
    {0x120B420U, 0x11E3520U, 0xE8U}, {0x120B42CU, 0x12703E0U, 0xE8U},
    {0x120B440U, 0x1212070U, 0xE9U}, {0x121059CU, 0x11E3520U, 0xE8U},
    {0x12105AAU, 0x1270410U, 0xE8U}, {0x12105B4U, 0x1212070U, 0xE8U},
    {0x12105CFU, 0x122C1A0U, 0xE8U}, {0x12106D0U, 0x11E3520U, 0xE8U},
    {0x12106DEU, 0x1270410U, 0xE8U}, {0x12106E8U, 0x1212070U, 0xE8U},
    {0x1210703U, 0x122C1A0U, 0xE8U},
}};

constexpr auto kResultCopyWindow = hex_bytes(
    "410FB64601410F10004D8D40504103C141FFC14863C8488B4638488BD14803C6"
    "48C1E2050F11440248410F1048C00F114C0258488B46484803C666834C485801");
constexpr std::uint32_t kResultCopyWindowRva = 0x58F030U;

struct ExpectedSection final {
    std::array<char, 8U> name{};
    std::uint32_t virtual_size{};
    std::uint32_t virtual_address{};
    std::uint32_t raw_size{};
    std::uint32_t raw_pointer{};
    std::uint32_t characteristics{};
};

constexpr std::array<ExpectedSection, kPinnedSectionCount> kExpectedSections{{
    {{{'.','t','e','x','t',0,0,0}}, 0x01B8F600U, 0x00001000U, 0x01B8F600U, 0x00000600U, 0x60000020U},
    {{{'.','r','d','a','t','a',0,0}}, 0x0037F800U, 0x01B91000U, 0x0037F800U, 0x01B8FC00U, 0x40000040U},
    {{{'.','d','a','t','a',0,0,0}}, 0x016BE9B0U, 0x01F11000U, 0x001AA600U, 0x01F0F400U, 0xC0000040U},
    {{{'.','p','d','a','t','a',0,0}}, 0x0014D800U, 0x035D0000U, 0x0014D800U, 0x020B9A00U, 0x40000040U},
    {{{'.','v','m','p','0',0,0,0}}, 0x00484000U, 0x0371E000U, 0x00484000U, 0x02207200U, 0x40000040U},
    {{{'.','t','l','s',0,0,0,0}}, 0x0009AC00U, 0x03BA2000U, 0x0009AC00U, 0x0268B200U, 0xC0000040U},
    {{{'_','R','D','A','T','A',0,0}}, 0x00000E00U, 0x03C3D000U, 0x00000E00U, 0x02725E00U, 0x40000040U},
    {{{'.','r','s','r','c',0,0,0}}, 0x0004A400U, 0x03C3E000U, 0x0004A400U, 0x02726C00U, 0x40000040U},
    {{{'.','r','e','l','o','c',0,0}}, 0x0003AC00U, 0x03C89000U, 0x0003AC00U, 0x02771000U, 0x40000040U},
    {{{'.','i','d','a','t','a',0,0}}, 0x00004400U, 0x03CC4000U, 0x00004400U, 0x027ABC00U, 0xC0000040U},
    {{{'.','t','e','x','t',0,0,0}}, 0x04D95A00U, 0x03CC9000U, 0x04D95A00U, 0x027B0000U, 0x60000020U},
}};

template <typename T>
[[nodiscard]] bool read_value(std::span<const std::byte> bytes,
                              std::size_t offset,
                              T& output) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&output, bytes.data() + offset, sizeof(T));
    return true;
}

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                    std::uint32_t amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

void sha256_block(std::array<std::uint32_t, 8U>& state,
                  const std::array<std::byte, 64U>& block) noexcept {
    constexpr std::array<std::uint32_t, 64U> constants{
        0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
        0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
        0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
        0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
        0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
        0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
        0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
        0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U};
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::size_t offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset])) << 24U)
                     | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 1U])) << 16U)
                     | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 2U])) << 8U)
                     | static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 3U]));
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
        const auto x = words[index - 15U];
        const auto y = words[index - 2U];
        const auto small0 = rotate_right(x, 7U) ^ rotate_right(x, 18U) ^ (x >> 3U);
        const auto small1 = rotate_right(y, 17U) ^ rotate_right(y, 19U) ^ (y >> 10U);
        words[index] = words[index - 16U] + small0 + words[index - 7U] + small1;
    }
    auto a = state[0]; auto b = state[1]; auto c = state[2]; auto d = state[3];
    auto e = state[4]; auto f = state[5]; auto g = state[6]; auto h = state[7];
    for (std::size_t index = 0U; index < words.size(); ++index) {
        const auto big1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const auto choice = (e & f) ^ ((~e) & g);
        const auto temporary1 = h + big1 + choice + constants[index] + words[index];
        const auto big0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = big0 + majority;
        h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

[[nodiscard]] Sha256 sha256(std::span<const std::byte> bytes) noexcept {
    std::array<std::uint32_t, 8U> state{
        0x6A09E667U,0xBB67AE85U,0x3C6EF372U,0xA54FF53AU,
        0x510E527FU,0x9B05688CU,0x1F83D9ABU,0x5BE0CD19U};
    std::array<std::byte, 64U> block{};
    std::size_t offset = 0U;
    while (bytes.size() - offset >= block.size()) {
        std::memcpy(block.data(), bytes.data() + offset, block.size());
        sha256_block(state, block);
        offset += block.size();
    }
    const std::size_t remaining = bytes.size() - offset;
    std::fill(block.begin(), block.end(), std::byte{0});
    if (remaining != 0U) {
        std::memcpy(block.data(), bytes.data() + offset, remaining);
    }
    block[remaining] = std::byte{0x80};
    if (remaining >= 56U) {
        sha256_block(state, block);
        std::fill(block.begin(), block.end(), std::byte{0});
    }
    const auto bit_count = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        block[63U - index] = std::byte{static_cast<std::uint8_t>(bit_count >> (index * 8U))};
    }
    sha256_block(state, block);
    Sha256 digest{};
    for (std::size_t index = 0U; index < state.size(); ++index) {
        digest[index * 4U] = std::byte{static_cast<std::uint8_t>(state[index] >> 24U)};
        digest[index * 4U + 1U] = std::byte{static_cast<std::uint8_t>(state[index] >> 16U)};
        digest[index * 4U + 2U] = std::byte{static_cast<std::uint8_t>(state[index] >> 8U)};
        digest[index * 4U + 3U] = std::byte{static_cast<std::uint8_t>(state[index])};
    }
    return digest;
}

struct PeView final { std::size_t section_table{}; };

[[nodiscard]] bool exact_pe(std::span<const std::byte> bytes, PeView& output) noexcept {
    std::uint16_t mz{};
    std::uint32_t pe_offset{};
    if (!read_value(bytes, 0U, mz) || mz != 0x5A4DU
        || !read_value(bytes, 0x3CU, pe_offset) || pe_offset != 0x190U) {
        return false;
    }
    std::uint32_t signature{}; std::uint16_t machine{}; std::uint16_t sections{};
    std::uint32_t timestamp{}; std::uint16_t optional_bytes{}; std::uint16_t magic{};
    std::uint32_t entry{}; std::uint64_t image_base{}; std::uint32_t size_image{};
    std::uint32_t size_headers{};
    const std::size_t file_header = static_cast<std::size_t>(pe_offset) + 4U;
    const std::size_t optional = file_header + 20U;
    if (!read_value(bytes, pe_offset, signature) || signature != 0x00004550U
        || !read_value(bytes, file_header, machine) || machine != kPinnedMachine
        || !read_value(bytes, file_header + 2U, sections) || sections != kPinnedSectionCount
        || !read_value(bytes, file_header + 4U, timestamp) || timestamp != kPinnedPeTimestamp
        || !read_value(bytes, file_header + 16U, optional_bytes) || optional_bytes != 0xF0U
        || !read_value(bytes, optional, magic) || magic != 0x20BU
        || !read_value(bytes, optional + 16U, entry) || entry != kPinnedEntryRva
        || !read_value(bytes, optional + 24U, image_base) || image_base != kPinnedPreferredImageBase
        || !read_value(bytes, optional + 56U, size_image) || size_image != kPinnedSizeOfImage
        || !read_value(bytes, optional + 60U, size_headers) || size_headers != kPinnedSizeOfHeaders) {
        return false;
    }
    output.section_table = optional + optional_bytes;
    for (std::size_t index = 0U; index < kExpectedSections.size(); ++index) {
        const std::size_t section = output.section_table + index * 40U;
        std::array<char, 8U> name{}; std::uint32_t virtual_size{}; std::uint32_t virtual_address{};
        std::uint32_t raw_size{}; std::uint32_t raw_pointer{}; std::uint32_t characteristics{};
        if (section > bytes.size() || 40U > bytes.size() - section) { return false; }
        std::memcpy(name.data(), bytes.data() + section, name.size());
        if (!read_value(bytes, section + 8U, virtual_size)
            || !read_value(bytes, section + 12U, virtual_address)
            || !read_value(bytes, section + 16U, raw_size)
            || !read_value(bytes, section + 20U, raw_pointer)
            || !read_value(bytes, section + 36U, characteristics)) { return false; }
        const auto& expected = kExpectedSections[index];
        if (name != expected.name || virtual_size != expected.virtual_size
            || virtual_address != expected.virtual_address || raw_size != expected.raw_size
            || raw_pointer != expected.raw_pointer || characteristics != expected.characteristics) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool executable_section_contains(std::uint32_t rva, std::size_t bytes) noexcept {
    const auto end64 = static_cast<std::uint64_t>(rva) + static_cast<std::uint64_t>(bytes);
    if (end64 > std::numeric_limits<std::uint32_t>::max()) { return false; }
    for (const auto& section : kExpectedSections) {
        if ((section.characteristics & 0x20000000U) == 0U) { continue; }
        const auto section_end = static_cast<std::uint64_t>(section.virtual_address) + section.virtual_size;
        if (rva >= section.virtual_address && end64 <= section_end) { return true; }
    }
    return false;
}

[[nodiscard]] bool mapped_range(std::span<const std::byte> mapped,
                                std::uint32_t rva,
                                std::size_t bytes,
                                std::span<const std::byte>& output) noexcept {
    const std::size_t offset = rva;
    if (offset > mapped.size() || bytes > mapped.size() - offset) { return false; }
    output = mapped.subspan(offset, bytes);
    return true;
}

[[nodiscard]] bool page_is_committed_read_execute(const std::byte* begin,
                                                  std::size_t bytes) noexcept {
#if defined(_WIN32)
    if (begin == nullptr || bytes == 0U) { return false; }
    const auto start = reinterpret_cast<std::uintptr_t>(begin);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - start) { return false; }
    const auto end = start + bytes;
    auto cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information, sizeof(information)) == 0U
            || information.State != MEM_COMMIT
            || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0U) { return false; }
        const DWORD base_protect = information.Protect & 0xFFU;
        if (base_protect != PAGE_EXECUTE_READ && base_protect != PAGE_EXECUTE_READWRITE
            && base_protect != PAGE_EXECUTE_WRITECOPY) { return false; }
        const auto region_begin = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        if (information.RegionSize > std::numeric_limits<std::uintptr_t>::max() - region_begin) { return false; }
        const auto region_end = region_begin + information.RegionSize;
        if (region_end <= cursor) { return false; }
        cursor = (std::min)(region_end, end);
    }
    return true;
#else
    (void)begin; (void)bytes;
    return false;
#endif
}

[[nodiscard]] bool add_unsigned(std::uintptr_t base,
                                std::uint64_t amount,
                                std::uintptr_t& output) noexcept {
    if (amount > std::numeric_limits<std::uintptr_t>::max() - base) { return false; }
    output = base + static_cast<std::uintptr_t>(amount);
    return true;
}

[[nodiscard]] bool add_signed(std::uintptr_t base,
                              std::uint32_t field_offset,
                              std::int64_t relative,
                              std::uintptr_t& output) noexcept {
    std::uintptr_t field{};
    if (!add_unsigned(base, field_offset, field)) { return false; }
    if (relative >= 0) { return add_unsigned(field, static_cast<std::uint64_t>(relative), output); }
    const auto magnitude = static_cast<std::uint64_t>(-(relative + 1)) + 1U;
    if (magnitude > field) { return false; }
    output = field - static_cast<std::uintptr_t>(magnitude);
    return true;
}

[[nodiscard]] bool scaled_address(std::uintptr_t base,
                                  std::uint32_t header,
                                  std::uint32_t index,
                                  std::uint32_t stride,
                                  std::uintptr_t& output) noexcept {
    const auto amount = static_cast<std::uint64_t>(header)
                      + static_cast<std::uint64_t>(index) * stride;
    return add_unsigned(base, amount, output);
}

[[nodiscard]] bool complete_handle(const HandleResolution& handle) noexcept {
    return handle.full_handle != 0U && handle.record_identity != 0U
           && handle.record_generation != 0U && handle.equality_rechecked_at_use;
}

[[nodiscard]] bool finite_transform(const Transform32& transform) noexcept {
    std::array<float, 8U> values{};
    std::memcpy(values.data(), transform.data(), transform.size());
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

[[nodiscard]] std::uint32_t load_u32(const std::byte* bytes) noexcept {
    std::uint32_t value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

[[nodiscard]] std::int64_t load_i64(const std::byte* bytes) noexcept {
    std::int64_t value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

class AtomicFlagGuard final {
public:
    explicit AtomicFlagGuard(std::atomic_flag& flag) noexcept : flag_(flag) {}
    ~AtomicFlagGuard() noexcept { flag_.clear(std::memory_order_release); }
    AtomicFlagGuard(const AtomicFlagGuard&) = delete;
    AtomicFlagGuard& operator=(const AtomicFlagGuard&) = delete;
private:
    std::atomic_flag& flag_;
};

[[nodiscard]] bool metadata_complete(const CaptureMetadata& metadata) noexcept {
    return metadata.capture_epoch != 0U && metadata.monotonic_tick != 0U
           && metadata.call_id != 0U && metadata.tls_nonce != 0U
           && metadata.producer_thread_id != 0U && metadata.surface != NativeSurface::count;
}

} // namespace

struct RuntimeAdmissionIssuer final {
    static void issue(RuntimeCohortToken& token) noexcept {
        token.cohort_id_ = 0x81964380664E7FCEULL;
    }
};

NativeTarget native_target(NativeSurface surface) noexcept {
    const auto index = static_cast<std::size_t>(surface);
    if (index >= kManifest.size()) { return {}; }
    const auto& entry = kManifest[index];
    return NativeTarget{entry.surface, entry.rva, entry.prefix, entry.bytes, entry.sha};
}

CaptureBoundaryDescriptor capture_boundary(NativeSurface surface) noexcept {
    switch (surface) {
    case NativeSurface::actor_terminal_invalidation:
        return {surface, kActorTerminalInvalidationRva,
                CapturePayload::actor_generation_invalidation, CaptureTiming::pre_native_entry,
                {kActorTerminalInvalidationCallsiteRva, 0U}, 1U, {}, 0U};
    case NativeSurface::source_writer:
        return {surface, 0x58EB40U, CapturePayload::source_route,
                CaptureTiming::entry_and_post_original, {0x58FAB5U, 0U}, 1U, {}, 0U};
    case NativeSurface::effect_create_one:
        return {surface, kEffectCreateOneRva, CapturePayload::effect_create_out_pair,
                CaptureTiming::entry_and_post_original, {0x1212CB0U, 0x1212DF7U}, 2U, {}, 0U};
    case NativeSurface::effect_explicit_destroy:
        return {surface, kEffectExplicitDestroyRva, CapturePayload::effect_full_handle_terminal,
                CaptureTiming::entry_and_post_original, {}, 0U, {0x120B540U, 0U}, 1U};
    case NativeSurface::effect_priority_eviction_destroy:
        return {surface, kEffectPriorityEvictionDestroyRva,
                CapturePayload::effect_full_handle_terminal,
                CaptureTiming::entry_and_post_original, {}, 0U, {0x120B420U, 0U}, 1U};
    case NativeSurface::effect_update_and_expiry:
        return {surface, kEffectUpdateAndExpiryRva, CapturePayload::effect_inlined_expiry,
                CaptureTiming::within_original_terminal_path, {}, 0U,
                {kEffectExpiryPathARva, kEffectExpiryPathBRva}, 2U};
    default:
        return {surface, native_target(surface).rva, CapturePayload::none,
                CaptureTiming::none, {}, 0U, {}, 0U};
    }
}

std::span<const DirectEdge> direct_edge_manifest() noexcept { return kDirectEdges; }
MappedWindow candidate_result_bank_copy_window() noexcept {
    return MappedWindow{kResultCopyWindowRva, kResultCopyWindow};
}

RuntimeAdmissionDiagnostic validate_runtime_admission(const RuntimeImageView& image,
                                                      RuntimeCohortToken& issued_token) noexcept {
    issued_token = {};
#if !defined(_WIN32)
    (void)image;
    return {RuntimeAdmissionResult::unsupported_platform, NativeSurface::count, 0U};
#else
    if (image.packed_file.size() != kPinnedPackedRuntimeBytes) {
        return {RuntimeAdmissionResult::packed_size_mismatch, NativeSurface::count, 0U};
    }
    if (sha256(image.packed_file) != kPinnedPackedRuntimeSha256) {
        return {RuntimeAdmissionResult::packed_hash_mismatch, NativeSurface::count, 0U};
    }
    PeView packed_pe{};
    if (!exact_pe(image.packed_file, packed_pe)) {
        return {RuntimeAdmissionResult::packed_pe_mismatch, NativeSurface::count, 0U};
    }
    if (image.mapped_image == nullptr || image.mapped_image_bytes != kPinnedSizeOfImage) {
        return {RuntimeAdmissionResult::mapped_bounds_mismatch, NativeSurface::count, 0U};
    }
    const std::span<const std::byte> mapped{image.mapped_image, image.mapped_image_bytes};
    PeView mapped_pe{};
    if (!exact_pe(mapped, mapped_pe)) {
        return {RuntimeAdmissionResult::mapped_pe_mismatch, NativeSurface::count, 0U};
    }
    if (mapped_pe.section_table != packed_pe.section_table) {
        return {RuntimeAdmissionResult::mapped_section_mismatch, NativeSurface::count, 0U};
    }
    for (const auto& entry : kManifest) {
        if (!executable_section_contains(entry.rva, entry.bytes)) {
            return {RuntimeAdmissionResult::target_not_in_executable_section, entry.surface, entry.rva};
        }
        std::span<const std::byte> function{};
        if (!mapped_range(mapped, entry.rva, entry.bytes, function)) {
            return {RuntimeAdmissionResult::mapped_bounds_mismatch, entry.surface, entry.rva};
        }
        if (!page_is_committed_read_execute(function.data(), function.size())) {
            return {RuntimeAdmissionResult::page_not_committed_read_execute, entry.surface, entry.rva};
        }
        if (function.size() < entry.prefix.size()
            || !std::equal(entry.prefix.begin(), entry.prefix.end(), function.begin())) {
            return {RuntimeAdmissionResult::function_prefix_mismatch, entry.surface, entry.rva};
        }
        if (sha256(function) != entry.sha) {
            return {RuntimeAdmissionResult::function_hash_mismatch, entry.surface, entry.rva};
        }
    }
    for (const auto& edge : kDirectEdges) {
        if (!executable_section_contains(edge.callsite_rva, 5U)
            || !executable_section_contains(edge.target_rva, 1U)) {
            return {RuntimeAdmissionResult::target_not_in_executable_section,
                    NativeSurface::count, edge.callsite_rva};
        }
        std::span<const std::byte> callsite{}; std::span<const std::byte> target{};
        if (!mapped_range(mapped, edge.callsite_rva, 5U, callsite)
            || !mapped_range(mapped, edge.target_rva, 1U, target)) {
            return {RuntimeAdmissionResult::mapped_bounds_mismatch,
                    NativeSurface::count, edge.callsite_rva};
        }
        if (!page_is_committed_read_execute(callsite.data(), callsite.size())
            || !page_is_committed_read_execute(target.data(), target.size())) {
            return {RuntimeAdmissionResult::page_not_committed_read_execute,
                    NativeSurface::count, edge.callsite_rva};
        }
        std::int32_t displacement{};
        std::memcpy(&displacement, callsite.data() + 1U, sizeof(displacement));
        const auto decoded = static_cast<std::int64_t>(edge.callsite_rva) + 5 + displacement;
        if (std::to_integer<std::uint8_t>(callsite[0]) != edge.opcode
            || decoded != static_cast<std::int64_t>(edge.target_rva)) {
            return {RuntimeAdmissionResult::callsite_mismatch,
                    NativeSurface::count, edge.callsite_rva};
        }
    }
    std::span<const std::byte> result_window{};
    if (!mapped_range(mapped, kResultCopyWindowRva, kResultCopyWindow.size(), result_window)
        || !executable_section_contains(kResultCopyWindowRva, kResultCopyWindow.size())
        || !page_is_committed_read_execute(result_window.data(), result_window.size())
        || !std::equal(result_window.begin(), result_window.end(), kResultCopyWindow.begin())) {
        return {RuntimeAdmissionResult::result_copy_window_mismatch,
                NativeSurface::source_writer, kResultCopyWindowRva};
    }
    RuntimeAdmissionIssuer::issue(issued_token);
    return {RuntimeAdmissionResult::admitted, NativeSurface::count, 0U};
#endif
}

bool early_effect_definition(EffectResourceDefinition definition) noexcept {
    return std::find(kEarlyEffectDefinitions.begin(), kEarlyEffectDefinitions.end(), definition)
           != kEarlyEffectDefinitions.end();
}

bool permanently_excluded_static_visual(const RouteSubject& subject) noexcept {
    return subject.effect_class == kPlacedVisualClass
        || subject.placed_anchor_or_target == kStaticFanAnchor
        || subject.placed_anchor_or_target == kStaticPortalTarget
        || std::find(kPlacedVisualDefinitions.begin(), kPlacedVisualDefinitions.end(),
                     subject.effect_resource.value) != kPlacedVisualDefinitions.end()
        || std::find(kPlacedVisualFullHandles.begin(), kPlacedVisualFullHandles.end(),
                     subject.placed_visual_full_handle) != kPlacedVisualFullHandles.end();
}

std::uint64_t coarse_probe_sample_delta(const ProbeSamplePair& pair) noexcept {
    if (pair.first_boundary != ProbeBoundary::after_transition_wrapper_and_schedule
        || pair.second_boundary != ProbeBoundary::before_successor_factory
        || pair.second_tick < pair.first_tick) { return 0U; }
    return pair.second_tick - pair.first_tick;
}

std::uint32_t fnv1_name_hash(std::span<const char> name) noexcept {
    std::uint32_t hash = 0x811C9DC5U;
    for (const char value : name) {
        hash *= 0x01000193U;
        hash ^= static_cast<std::uint8_t>(value);
    }
    return hash;
}

LifecycleTokenOwner::LifecycleTokenOwner(RuntimeCohortToken cohort,
                                         std::uint64_t host_session,
                                         std::uint64_t activation,
                                         std::uint64_t region) noexcept
    : cohort_(cohort), host_session_(host_session), activation_(activation), region_(region) {}

bool LifecycleTokenOwner::try_snapshot(OwnerScopeToken& output) const noexcept {
    output = {};
    const auto before = version_.load(std::memory_order_acquire);
    if ((before & 1U) != 0U || !cohort_.valid() || host_session_ == 0U) { return false; }
    OwnerScopeToken candidate{};
    candidate.cohort_id_ = cohort_.cohort_id();
    candidate.host_session_ = host_session_;
    candidate.activation_ = activation_.load(std::memory_order_acquire);
    candidate.region_ = region_.load(std::memory_order_acquire);
    candidate.issue_serial_ = next_issue_.load(std::memory_order_acquire);
    const auto after = version_.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0U || !candidate.valid()) { return false; }
    output = candidate;
    return true;
}

void LifecycleTokenOwner::publish(std::uint64_t activation, std::uint64_t region) noexcept {
    version_.fetch_add(1U, std::memory_order_acq_rel);
    activation_.store(activation, std::memory_order_release);
    region_.store(region, std::memory_order_release);
    next_issue_.fetch_add(1U, std::memory_order_acq_rel);
    version_.fetch_add(1U, std::memory_order_release);
}

bool LifecycleTokenOwner::is_current(OwnerScopeToken token) const noexcept {
    OwnerScopeToken current{};
    return try_snapshot(current) && current == token;
}

bool AddressRange::contains(std::uintptr_t address, std::size_t bytes) const noexcept {
    if (begin == 0U || end <= begin || address < begin || address > end) { return false; }
    return bytes <= end - address;
}

RawObservationResult validate_raw_observation(const RouteCaptureRecord& record) noexcept {
    if (permanently_excluded_static_visual(record.subject)) {
        return RawObservationResult::static_fan_excluded;
    }
    if (!metadata_complete(record.metadata)) { return RawObservationResult::invalid_metadata; }
    if (!record.entry_scope.valid() || record.entry_scope.cohort_id() == 0U) {
        return RawObservationResult::invalid_scope_token;
    }
    if (record.metadata.surface != NativeSurface::source_writer
        || record.metadata.callsite_rva != 0x58FAB5U) {
        return RawObservationResult::wrong_surface;
    }
    return RawObservationResult::retained;
}
RouteClosureResult close_route(const RouteCaptureRecord& record,
                               OwnerScopeToken current_scope) noexcept {
    if (validate_raw_observation(record) != RawObservationResult::retained) {
        return RouteClosureResult::raw_observation_invalid;
    }
    if (!current_scope.valid() || record.entry_scope != record.exit_scope
        || record.entry_scope != current_scope || record.presentation.scope != record.entry_scope
        || record.provider.scope != record.entry_scope || record.provider.owner.scope != record.entry_scope) {
        return RouteClosureResult::stale_scope;
    }
    const auto& actor = record.provider.owner;
    if (actor.full_handle == 0U || actor.object_record_identity == 0U
        || actor.object_record_generation == 0U || actor.factory_create_serial == 0U
        || actor.terminal_serial != 0U || record.provider.component_definition == 0U
        || record.provider.component_full_handle == 0U || record.provider.component_generation == 0U
        || !record.provider_owner_handle_current_at_entry
        || !record.provider_owner_handle_current_at_exit) {
        return RouteClosureResult::retired_or_unbound_owner;
    }
    if (record.source.machine_definition != kPresentationMachine
        || record.source.machine_class != kPresentationClass
        || !record.source.descriptor_read_complete
        || record.source.descriptor != kExactOutput1Descriptor) {
        return RouteClosureResult::wrong_machine_or_descriptor;
    }
    if (!record.source.runtime_read_complete || record.source.source_row != kRuntimeSourceRow
        || record.source.output_start != kOutputSlot || record.source.output_count != 1U
        || record.source.source_kind != 2U) {
        return RouteClosureResult::wrong_source_row;
    }
    const auto& bank = record.bank;
    if (!bank.before_read_complete || !bank.after_read_complete || bank.transform_count <= kOutputSlot) {
        return RouteClosureResult::incomplete_bank_read;
    }
    std::uintptr_t transform_base{}; std::uintptr_t validity_base{}; std::uintptr_t runtime_base{};
    std::uintptr_t expected_output{}; std::uintptr_t expected_validity{};
    std::uintptr_t expected_runtime_rows{}; std::uintptr_t expected_runtime{};
    if (!add_signed(bank.bank_identity, kPcBankTransformRelativeOffset,
                    bank.transform_relative, transform_base)
        || !add_signed(bank.bank_identity, kPcBankValidityRelativeOffset,
                       bank.validity_relative, validity_base)
        || !add_signed(bank.bank_identity, kPcBankRuntimeRelativeOffset,
                       bank.runtime_relative, runtime_base)
        || !scaled_address(transform_base, kPcBankVectorHeaderBytes, kOutputSlot,
                           kTransformBytes, expected_output)
        || !scaled_address(validity_base, kPcBankVectorHeaderBytes, kOutputSlot,
                           kValidityBytes, expected_validity)
        || !add_unsigned(runtime_base, kPcBankVectorHeaderBytes, expected_runtime_rows)
        || !scaled_address(expected_runtime_rows, 0U, kRuntimeSourceRow,
                           kRuntimeRowBytes, expected_runtime)
        || transform_base != bank.transform_base_identity
        || validity_base != bank.validity_base_identity
        || runtime_base != bank.runtime_base_identity
        || expected_runtime_rows != bank.runtime_rows_identity
        || expected_output != bank.selected_output_identity
        || expected_validity != bank.selected_validity_identity
        || expected_runtime != bank.selected_runtime_identity
        || !bank.transform_allocation.contains(expected_output, kTransformBytes)
        || !bank.validity_allocation.contains(expected_validity, kValidityBytes)
        || !bank.runtime_allocation.contains(expected_runtime, kRuntimeRowBytes)) {
        return RouteClosureResult::bank_address_equation_mismatch;
    }
    const auto& provider = record.provider;
    std::uintptr_t expected_provider{}; std::uintptr_t expected_definition{};
    if (!provider.provider_read_complete || !provider.definition_target_read_complete
        || load_u32(record.source.runtime.data()) != provider.runtime_provider_handle.full_handle
        || load_i64(record.source.runtime.data() + 0x08U) != provider.runtime_provider_relative
        || load_u32(record.source.runtime.data() + 0x10U) != record.resolver.runtime_selector
        || load_u32(provider.provider_bytes.data()) != provider.definition_handle.full_handle
        || load_i64(provider.provider_bytes.data() + 0x08U) != provider.definition_relative
        || load_u32(provider.provider_bytes.data() + kProviderPoseDispatchOffset)
               != provider.pose_dispatch_handle.full_handle
        || load_u32(provider.provider_bytes.data() + kProviderPoseObjectHandleOffset)
               != provider.pose_object_handle.full_handle
        || load_i64(provider.provider_bytes.data() + kProviderPoseObjectRelativeOffset)
               != provider.pose_object_relative
        || load_i64(provider.provider_bytes.data() + kProviderFilterRelativeOffset)
               != provider.filter_relative
        || load_i64(provider.definition_target_bytes.data() + kDefinitionTableRelativeOffset)
               != provider.definition_table_relative
        || !complete_handle(provider.runtime_provider_handle)
        || !complete_handle(provider.definition_handle)
        || !add_signed(provider.runtime_provider_handle.record_identity, 0U,
                       provider.runtime_provider_relative, expected_provider)
        || !add_signed(provider.definition_handle.record_identity, 0U,
                       provider.definition_relative, expected_definition)
        || expected_provider != provider.provider_identity
        || expected_definition != provider.definition_target_identity
        || provider.provider_generation == 0U || provider.definition_target_generation == 0U
        || !provider.component_allocation.contains(provider.provider_identity, 0x78U)) {
        return RouteClosureResult::provider_address_equation_mismatch;
    }
    std::uintptr_t expected_table{}; std::uintptr_t expected_selection{};
    std::uintptr_t expected_candidate{};
    if (record.resolver.resolver_stride == 0U
        || !add_signed(provider.definition_target_identity, kDefinitionTableRelativeOffset,
                       provider.definition_table_relative, expected_table)
        || !scaled_address(expected_table, kSelectionTableRowsOffset,
                           record.resolver.runtime_selector, record.resolver.resolver_stride,
                           expected_selection)
        || !scaled_address(expected_table, kCandidateTableRowsOffset,
                           record.resolver.candidate_ordinal, record.resolver.resolver_stride,
                           expected_candidate)
        || expected_table != provider.definition_table_identity
        || expected_selection != record.resolver.selection_address
        || expected_candidate != record.resolver.candidate_row_identity
        || provider.definition_table_generation == 0U
        || !provider.definition_table_allocation.contains(expected_selection, sizeof(std::uint32_t))
        || !provider.definition_table_allocation.contains(expected_candidate,
                                                           record.resolver.candidate_row.size())) {
        return RouteClosureResult::table_address_equation_mismatch;
    }
    std::uintptr_t expected_pose{}; std::uintptr_t expected_filter{};
    if (!complete_handle(provider.pose_dispatch_handle) || !complete_handle(provider.pose_object_handle)
        || !add_signed(provider.pose_object_handle.record_identity, 0U,
                       provider.pose_object_relative, expected_pose)
        || !add_signed(provider.provider_identity, kProviderFilterRelativeOffset,
                       provider.filter_relative, expected_filter)
        || provider.pose_dispatch_base_identity != provider.pose_dispatch_handle.record_identity
        || provider.pose_dispatch_base_identity != record.resolver.pose_interface_dispatch_base
        || expected_pose != provider.concrete_pose_object_identity
        || expected_pose != record.resolver.pose_interface_object
        || expected_filter != provider.filter_context_identity) {
        return RouteClosureResult::dispatch_or_pose_equation_mismatch;
    }
    if (record.resolver.selector_mode != RuntimeSelectorMode::ordinary
        || record.resolver.result_status != NativeResultStatus::results_returned
        || record.resolver.native_result_count <= 0
        || static_cast<std::uint32_t>(record.resolver.native_result_count)
               > record.resolver.output_capacity) {
        return RouteClosureResult::sentinel_or_failed_result;
    }
    if (record.resolver.candidate_interval_end <= record.resolver.candidate_interval_begin
        || record.resolver.candidate_ordinal < record.resolver.candidate_interval_begin
        || record.resolver.candidate_ordinal >= record.resolver.candidate_interval_end
        || load_u32(record.resolver.candidate_row.data() + kCandidateBindingKeyOffset)
               != record.resolver.binding_key
        || load_u32(record.resolver.candidate_row.data() + kCandidateSelectionKeyOffset)
               != record.resolver.candidate_selection_key
        || !std::equal(record.resolver.candidate_local.begin(), record.resolver.candidate_local.end(),
                       record.resolver.candidate_row.begin() + kCandidateLocalTransformOffset)) {
        return RouteClosureResult::candidate_address_equation_mismatch;
    }
    if (!record.resolver.candidate_row_read_complete
        || !record.resolver.candidate_result_after_read_complete
        || !record.resolver.pose_output_after_read_complete) {
        return RouteClosureResult::incomplete_nested_read;
    }
    Transform32 result_transform{};
    std::copy_n(record.resolver.candidate_result_after.begin() + kCandidateResultTransformOffset,
                result_transform.size(), result_transform.begin());
    if (!finite_transform(record.resolver.candidate_local) || !finite_transform(result_transform)
        || !finite_transform(record.resolver.pose_output_after)
        || !finite_transform(record.bank.output_after)) {
        return RouteClosureResult::nonfinite_transform;
    }
    if (result_transform != record.bank.output_after || (record.bank.validity_after & 1U) == 0U) {
        return RouteClosureResult::result_bank_copy_mismatch;
    }
    if (!record.original.valid_for(record.metadata.call_id)) {
        return RouteClosureResult::original_once_not_proven;
    }
    return RouteClosureResult::closed;
}

bool resolves_exact_left_hand_joint(const RouteCaptureRecord& record) noexcept {
    const auto& provider = record.provider;
    const auto& resolver = record.resolver;
    return provider.pose_resource_definition == kPoseResourceDefinition
        && provider.pose_resource_generation != 0U
        && provider.pose_resource_generation == resolver.pose_resource_generation
        && resolver.pose_header_identity == kPoseHeaderIdentity
        && resolver.pose_object_bytes == kPoseObjectBytes
        && resolver.binding_key == kLeftHandJointIndex
        && fnv1_name_hash(std::span{kLeftHandJointName, sizeof(kLeftHandJointName) - 1U})
               == kLeftHandJointNameFnv1;
}

ProviderFreshnessKey provider_freshness_key(const RouteCaptureRecord& record) noexcept {
    return {record.provider.owner.full_handle,
            record.provider.owner.object_record_generation,
            record.provider.component_generation,
            record.provider.runtime_provider_handle.full_handle,
            record.provider.runtime_provider_handle.record_generation,
            record.provider.provider_generation,
            record.provider.definition_target_generation,
            record.provider.definition_table_generation,
            record.provider.pose_dispatch_handle.record_generation,
            record.provider.pose_object_handle.record_generation,
            record.provider.pose_resource_generation,
            record.presentation.generation,
            record.presentation.bank_generation};
}

AuxiliaryValidationResult validate_effect_create(const EffectCreateCapture& capture) noexcept {
    if (!metadata_complete(capture.metadata)) { return AuxiliaryValidationResult::invalid_metadata; }
    if (!capture.scope.valid()) { return AuxiliaryValidationResult::invalid_scope; }
    if (capture.metadata.surface != NativeSurface::effect_create_one
        || (capture.metadata.callsite_rva != 0x1212CB0U
            && capture.metadata.callsite_rva != 0x1212DF7U)) {
        return AuxiliaryValidationResult::wrong_boundary;
    }
    if ((capture.metadata.callsite_rva == 0x1212CB0U) != capture.first_or_child_call) {
        return AuxiliaryValidationResult::wrong_boundary;
    }
    if (!early_effect_definition(capture.resource)) {
        return AuxiliaryValidationResult::invalid_resource_or_handle;
    }
    if (!capture.out_pair_before_read_complete || !capture.out_pair_after_read_complete) {
        return AuxiliaryValidationResult::invalid_out_pair;
    }
    if (capture.native_success
        && (effect_handle_from_out_pair(capture.out_pair_after).value == 0U
            || capture.resolved_effect_identity == 0U || capture.pool_generation == 0U
            || capture.create_serial == 0U || capture.owner_actor_full_handle == 0U
            || capture.owner_actor_record_generation == 0U
            || capture.owner_provider_generation == 0U)) {
        return AuxiliaryValidationResult::invalid_out_pair;
    }
    if (!capture.original.valid_for(capture.metadata.call_id)) {
        return AuxiliaryValidationResult::original_once_not_proven;
    }
    return AuxiliaryValidationResult::valid;
}

AuxiliaryValidationResult validate_effect_terminal(const EffectTerminalCapture& capture) noexcept {
    if (!metadata_complete(capture.metadata)) { return AuxiliaryValidationResult::invalid_metadata; }
    if (!capture.scope.valid()) { return AuxiliaryValidationResult::invalid_scope; }
    NativeSurface expected_surface = NativeSurface::effect_explicit_destroy;
    std::uint32_t expected_path = 0x120B540U;
    switch (capture.kind) {
    case EffectTerminalKind::explicit_destroy: break;
    case EffectTerminalKind::priority_eviction_destroy:
        expected_surface = NativeSurface::effect_priority_eviction_destroy; expected_path = 0x120B420U; break;
    case EffectTerminalKind::automatic_expiry_path_a:
        expected_surface = NativeSurface::effect_update_and_expiry; expected_path = kEffectExpiryPathARva; break;
    case EffectTerminalKind::automatic_expiry_path_b:
        expected_surface = NativeSurface::effect_update_and_expiry; expected_path = kEffectExpiryPathBRva; break;
    }
    if (capture.metadata.surface != expected_surface) { return AuxiliaryValidationResult::wrong_boundary; }
    if (capture.terminal_path_rva != expected_path) { return AuxiliaryValidationResult::wrong_terminal_path; }
    if (capture.full_handle.value == 0U || capture.resolved_effect_identity == 0U
        || capture.pool_generation == 0U || capture.terminal_serial == 0U) {
        return AuxiliaryValidationResult::invalid_resource_or_handle;
    }
    if (!capture.original.valid_for(capture.metadata.call_id)) {
        return AuxiliaryValidationResult::original_once_not_proven;
    }
    return AuxiliaryValidationResult::valid;
}

AuxiliaryValidationResult validate_actor_invalidation(const ActorInvalidationCapture& capture) noexcept {
    if (!metadata_complete(capture.metadata)) { return AuxiliaryValidationResult::invalid_metadata; }
    if (!capture.retiring_scope.valid() || capture.retiring_actor.scope != capture.retiring_scope) {
        return AuxiliaryValidationResult::invalid_scope;
    }
    if (capture.metadata.surface != NativeSurface::actor_terminal_invalidation
        || capture.metadata.callsite_rva != kActorTerminalInvalidationCallsiteRva) {
        return AuxiliaryValidationResult::wrong_boundary;
    }
    if (capture.wrapper_scene_tag != capture.retiring_actor.scheduler_tag
        || capture.wrapper_full_actor_handle != capture.retiring_actor.full_handle
        || capture.wrapper_full_actor_handle == 0U) {
        return AuxiliaryValidationResult::invalid_resource_or_handle;
    }
    if (!capture.rooted_generation_invalidated_before_original
        || capture.invalidated_provider_generation == 0U
        || capture.invalidated_bank_generation == 0U) {
        return AuxiliaryValidationResult::invalidation_not_pre_native;
    }
    if (!capture.original.valid_for(capture.metadata.call_id)) {
        return AuxiliaryValidationResult::original_once_not_proven;
    }
    return AuxiliaryValidationResult::valid;
}

EffectLedgerResult EffectLifetimeLedger::try_record_create(const EffectCreateCapture& capture) noexcept {
    if (validate_effect_create(capture) != AuxiliaryValidationResult::valid || !capture.native_success) {
        return EffectLedgerResult::invalid_capture;
    }
    if (!try_lock()) { return EffectLedgerResult::busy; }
    AtomicFlagGuard guard{lock_};
    const auto handle = effect_handle_from_out_pair(capture.out_pair_after);
    for (std::size_t index = 0U; index < count_; ++index) {
        if (entries_[index].full_handle == handle && entries_[index].pool_generation == capture.pool_generation
            && !entries_[index].terminal_observed) { return EffectLedgerResult::duplicate_live_handle; }
    }
    if (count_ == entries_.size()) { return EffectLedgerResult::full; }
    entries_[count_++] = EffectLifetime{capture.scope, capture.resource, handle,
                                       capture.resolved_effect_identity,
                                       effect_serial_from_out_pair(capture.out_pair_after),
                                       capture.pool_generation, capture.create_serial,
                                       capture.owner_actor_full_handle,
                                       capture.owner_actor_record_generation,
                                       capture.owner_provider_generation, 0U,
                                       EffectTerminalKind::explicit_destroy, false};
    return EffectLedgerResult::recorded;
}

EffectLedgerResult EffectLifetimeLedger::try_record_terminal(const EffectTerminalCapture& capture) noexcept {
    if (validate_effect_terminal(capture) != AuxiliaryValidationResult::valid) {
        return EffectLedgerResult::invalid_capture;
    }
    if (!try_lock()) { return EffectLedgerResult::busy; }
    AtomicFlagGuard guard{lock_};
    for (std::size_t index = 0U; index < count_; ++index) {
        auto& entry = entries_[index];
        if (entry.full_handle == capture.full_handle && entry.pool_generation == capture.pool_generation) {
            if (entry.terminal_observed) { return EffectLedgerResult::already_terminal; }
            if (entry.scope != capture.scope
                || entry.resolved_effect_identity != capture.resolved_effect_identity) {
                return EffectLedgerResult::unmatched_terminal;
            }
            entry.terminal_observed = true;
            entry.terminal_serial = capture.terminal_serial;
            entry.terminal_kind = capture.kind;
            return EffectLedgerResult::recorded;
        }
    }
    return EffectLedgerResult::unmatched_terminal;
}

bool EffectLifetimeLedger::try_find(EffectInstanceHandle32 handle,
                                    std::uint64_t pool_generation,
                                    EffectLifetime& output) noexcept {
    if (!try_lock()) { return false; }
    AtomicFlagGuard guard{lock_};
    for (std::size_t index = 0U; index < count_; ++index) {
        if (entries_[index].full_handle == handle && entries_[index].pool_generation == pool_generation) {
            output = entries_[index]; return true;
        }
    }
    return false;
}

SuccessorEligibilityResult evaluate_successor_eligibility(
    const ActorInvalidationCapture& invalidation,
    const ProviderFreshnessKey& retiring_key,
    const RouteCaptureRecord& successor,
    OwnerScopeToken current_scope) noexcept {
    if (validate_actor_invalidation(invalidation) != AuxiliaryValidationResult::valid
        || invalidation.retiring_actor.scheduler_tag != kInitialVisibleSchedulerTag
        || invalidation.retiring_actor.full_handle != kInitialVisibleFullHandle
        || invalidation.invalidated_provider_generation != retiring_key.provider_generation
        || invalidation.invalidated_bank_generation != retiring_key.bank_generation) {
        return SuccessorEligibilityResult::invalid_invalidation;
    }
    if (close_route(successor, current_scope) != RouteClosureResult::closed) {
        return SuccessorEligibilityResult::successor_route_not_closed;
    }
    const auto& actor = successor.provider.owner;
    if (actor.scheduler_tag == kCarrierSchedulerTag || actor.full_handle == kCarrierFullHandle) {
        return SuccessorEligibilityResult::carrier_fallback_rejected;
    }
    if (successor.ownership.carrier_exact_provider_owner_proven) {
        return SuccessorEligibilityResult::carrier_fallback_rejected;
    }
    if (actor.scheduler_tag != kSuccessorVisibleSchedulerTag
        || actor.full_handle != kSuccessorVisibleFullHandle
        || actor.entity_definition != kSharedEntityDefinition
        || actor.low_index != kReusedVisibleLowIndex
        || successor.provider.component_definition != kProviderComponentDefinition) {
        return SuccessorEligibilityResult::wrong_successor_tuple;
    }
    if (successor.resolver.provider_selection_callsite_rva != kProviderSelectionExactCallsiteRva
        || successor.resolver.provider_route_ordinal_for_actor != 1U) {
        return SuccessorEligibilityResult::not_first_exact_route;
    }
    const auto successor_key = provider_freshness_key(successor);
    if (successor_key == retiring_key || successor_key.actor_full_handle == retiring_key.actor_full_handle
        || successor_key.actor_record_generation == 0U || successor_key.component_generation == 0U
        || successor_key.provider_full_handle == 0U || successor_key.provider_record_generation == 0U
        || successor_key.provider_generation == 0U
        || successor_key.definition_target_generation == 0U
        || successor_key.definition_table_generation == 0U
        || successor_key.pose_dispatch_generation == 0U || successor_key.pose_object_generation == 0U
        || successor_key.pose_resource_generation == 0U || successor_key.presentation_generation == 0U
        || successor_key.bank_generation == 0U) {
        return SuccessorEligibilityResult::stale_or_reused_generation;
    }
    if (successor.resolver.selection_value != kPurpleSelection
        || successor.resolver.candidate_ordinal != kSourceRetainedCandidateOrdinal
        || successor.resolver.candidate_selection_key != kPurpleSelection
        || successor.resolver.binding_key != kSourceRetainedBindingKey
        || successor.resolver.allow_fallback || !resolves_exact_left_hand_joint(successor)) {
        return SuccessorEligibilityResult::wrong_selection_candidate_or_joint;
    }
    return SuccessorEligibilityResult::eligible_capture_only_emission_not_observed;
}

QueuePushResult CaptureQueue::try_push(const RouteCaptureRecord& record) noexcept {
    const auto validation = validate_raw_observation(record);
    if (validation == RawObservationResult::static_fan_excluded) {
        static_fan_excluded_.fetch_add(1U, std::memory_order_relaxed);
        return QueuePushResult::static_fan_excluded;
    }
    if (validation != RawObservationResult::retained) {
        rejected_.fetch_add(1U, std::memory_order_relaxed);
        return QueuePushResult::rejected;
    }
    if (!try_lock()) {
        dropped_busy_.fetch_add(1U, std::memory_order_relaxed);
        return QueuePushResult::busy;
    }
    AtomicFlagGuard guard{lock_};
    if (count_ == records_.size()) {
        dropped_full_.fetch_add(1U, std::memory_order_relaxed);
        return QueuePushResult::full;
    }
    if (next_sequence_ == 0U || next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        dropped_sequence_exhausted_.fetch_add(1U, std::memory_order_relaxed);
        return QueuePushResult::sequence_exhausted;
    }
    const std::size_t tail = (head_ + count_) % records_.size();
    records_[tail] = record;
    records_[tail].sequence = next_sequence_++;
    ++count_;
    accepted_.fetch_add(1U, std::memory_order_relaxed);
    return QueuePushResult::enqueued;
}

QueuePopResult CaptureQueue::try_pop(RouteCaptureRecord& output) noexcept {
    if (!try_lock()) { return QueuePopResult::busy; }
    AtomicFlagGuard guard{lock_};
    if (count_ == 0U) { return QueuePopResult::empty; }
    output = records_[head_];
    head_ = (head_ + 1U) % records_.size();
    --count_;
    return QueuePopResult::success;
}

QueueCounters CaptureQueue::counters() const noexcept {
    return {accepted_.load(std::memory_order_relaxed), rejected_.load(std::memory_order_relaxed),
            static_fan_excluded_.load(std::memory_order_relaxed),
            dropped_full_.load(std::memory_order_relaxed), dropped_busy_.load(std::memory_order_relaxed),
            dropped_sequence_exhausted_.load(std::memory_order_relaxed)};
}

std::uint64_t bounded_local_hash(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint32_t OpaqueIdProjector::project(std::uint64_t sensitive_scalar) noexcept {
    if (sensitive_scalar == 0U) { return 0U; }
    for (std::size_t index = 0U; index < count_; ++index) {
        if (entries_[index].value == sensitive_scalar) { return entries_[index].id; }
    }
    if (count_ == entries_.size() || next_id_ == 0U) { return 0U; }
    const auto id = next_id_++;
    entries_[count_++] = Entry{sensitive_scalar, id};
    return id;
}

bool default_telemetry(const RouteCaptureRecord& record,
                       OwnerScopeToken current_scope,
                       std::uint64_t queue_loss_epoch,
                       OpaqueIdProjector& projector,
                       RouteTelemetry& output) noexcept {
    if (record.sequence == 0U || validate_raw_observation(record) != RawObservationResult::retained) {
        output = {}; return false;
    }
    const auto closure = close_route(record, current_scope);
    output = {};
    output.sequence = record.sequence;
    output.cohort_id = record.entry_scope.cohort_id();
    output.capture_epoch = record.metadata.capture_epoch;
    output.monotonic_tick = record.metadata.monotonic_tick;
    output.call_id = record.metadata.call_id;
    output.activation = record.entry_scope.activation();
    output.region = record.entry_scope.region();
    output.presentation_generation = record.presentation.generation;
    output.bank_generation = record.presentation.bank_generation;
    output.descriptor_hash = bounded_local_hash(record.source.descriptor);
    output.runtime_hash = bounded_local_hash(record.source.runtime);
    output.candidate_row_hash = bounded_local_hash(record.resolver.candidate_row);
    output.candidate_result_hash = bounded_local_hash(record.resolver.candidate_result_after);
    output.bank_output_hash = bounded_local_hash(record.bank.output_after);
    output.queue_loss_epoch = queue_loss_epoch;
    output.actor_opaque_id = projector.project(record.provider.owner.full_handle);
    output.provider_opaque_id = projector.project(record.provider.runtime_provider_handle.full_handle);
    output.pose_opaque_id = projector.project(record.provider.pose_object_handle.full_handle);
    output.surface_rva = native_target(record.metadata.surface).rva;
    output.callsite_rva = record.metadata.callsite_rva;
    output.runtime_selector = record.resolver.runtime_selector;
    output.selection = record.resolver.selection_value;
    output.candidate_ordinal = record.resolver.candidate_ordinal;
    output.binding_key = record.resolver.binding_key;
    output.output_slot = record.source.output_start;
    output.surface = record.metadata.surface;
    output.status = closure == RouteClosureResult::closed ? TelemetryRecordStatus::route_closed
                  : record.entry_scope == current_scope ? TelemetryRecordStatus::raw_retained
                                                        : TelemetryRecordStatus::stale_or_partial;
    output.native_result = record.resolver.result_status;
    output.descriptor_valid = record.source.descriptor_read_complete;
    output.runtime_valid = record.source.runtime_read_complete;
    output.candidate_valid = record.resolver.candidate_row_read_complete;
    output.result_valid = record.resolver.candidate_result_after_read_complete;
    output.bank_valid = record.bank.after_read_complete;
    output.owner_current_at_entry = record.provider_owner_handle_current_at_entry;
    output.owner_current_at_exit = record.provider_owner_handle_current_at_exit;
    output.fallback_used = record.resolver.allow_fallback;
    output.left_hand_joint_valid = resolves_exact_left_hand_joint(record);
    output.carrier_concurrent = record.ownership.carrier_concurrent;
    output.carrier_presentation_correlated = record.ownership.carrier_presentation_correlated;
    output.carrier_exact_provider_owner_proven =
        record.ownership.carrier_exact_provider_owner_proven;
    output.observed_provider_factory_ordinal =
        record.ownership.observed_provider_factory_ordinal;
    return true;
}

} // namespace dawn::client::hooks::bootflow::opening_authority::ikora_vfx_route
