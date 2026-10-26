#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/activity/forced/activity_forced_destination.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
namespace {

/**
 * Periodic activity-metadata evaluator. During a late Homecoming zone swap its caller can retain
 * an active-row bit after the manager's key-to-row mapping has been reset to -1. Retail then uses
 * row -1, resolves a null provider, and virtual-calls it at +0x170CCD5.
 */
constexpr std::uintptr_t kProviderConsumerRva = 0x170CC70U;
constexpr std::array<std::byte, 20> kProviderConsumerPrefix{
    std::byte{0x40}, std::byte{0x55}, std::byte{0x53}, std::byte{0x57},
    std::byte{0x48}, std::byte{0x8D}, std::byte{0xAC}, std::byte{0x24},
    std::byte{0xE0}, std::byte{0xFD}, std::byte{0xFF}, std::byte{0xFF},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xEC}, std::byte{0x20},
    std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x48}};

constexpr std::size_t kKeyToMetadataMapOffset = 0x114U;
constexpr std::size_t kKeyToMetadataStride = 6U;
constexpr std::size_t kProviderTableOffset = 0x10U;
constexpr std::uint32_t kKeyIndexMask = 0x1FFFU;
constexpr std::int16_t kMaximumMetadataIndex = 1023;
constexpr std::uint32_t kLogLimit = 64U;

using ProviderConsumer = void(__fastcall*)(std::byte* manager, std::uint32_t key) noexcept;

hooking::detour::Handle g_handle{};
std::atomic<ProviderConsumer> g_original{nullptr};
std::atomic_uint32_t g_suppressed{};
std::atomic_bool g_installed{};

template <typename Value>
[[nodiscard]] Value safe_read(const void* address, Value fallback = {}) noexcept {
    Value value = fallback;
    __try {
        if (address != nullptr) {
            value = *static_cast<const Value*>(address);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = fallback;
    }
    return value;
}
[[nodiscard]] bool towerfall_forced(std::string_view& package) noexcept {
    namespace forced = state::activity::forced;
    if (!forced::override_active()) {
        return false;
    }
    forced::ForcedDestination destination{};
    forced::snapshot(destination);
    const std::size_t length = destination.packageNameLength <= destination.packageName.size()
                                   ? destination.packageNameLength
                                   : destination.packageName.size();
    package = std::string_view(destination.packageName.data(), length);
    return package == "mission_towerfall" || package == "cine_110_twr";
}

void log_suppressed(std::byte* manager,
                    std::uint32_t key,
                    std::int16_t metadataIndex,
                    const void* providerTable,
                    std::string_view package) noexcept {
    const std::uint32_t observation =
        g_suppressed.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (observation > kLogLimit && (observation & 0xFFU) != 0U) {
        return;
    }

    std::array<char, core::log::kLineCapacity> line{};
    const int length = std::snprintf(
        line.data(),
        line.size(),
        "ev=bootflow stage=activity_provider_stale_mapping_guard n=%u result=suppressed "
        "manager=%p key=0x%08X key_index=%u metadata_index=%d provider_table=%p "
        "forced=%.*s mutation=skip_invalid_native_call",
        observation,
        static_cast<void*>(manager),
        key,
        key & kKeyIndexMask,
        static_cast<int>(metadataIndex),
        providerTable,
        static_cast<int>(package.size()),
        package.data());
    if (length > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(length)});
    }
}

__declspec(noinline) void __fastcall provider_consumer(std::byte* manager,
                                                        std::uint32_t key) noexcept {
    const ProviderConsumer original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }

    std::string_view package{};
    if (!towerfall_forced(package) || manager == nullptr) {
        original(manager, key);
        return;
    }

    const std::uint32_t keyIndex = key & kKeyIndexMask;
    const std::int16_t metadataIndex = safe_read<std::int16_t>(
        manager + kKeyToMetadataMapOffset + keyIndex * kKeyToMetadataStride,
        static_cast<std::int16_t>(-1));
    const std::byte* const providerTable =
        safe_read<std::byte*>(manager + kProviderTableOffset, nullptr);

    // The crashing run had metadataIndex == -1. Indices outside the scanner's 1024-row table are
    // equally invalid. Do not inspect row -1 or invent a replacement provider; skip only this
    // stale evaluation and leave every valid native path untouched.
    if (metadataIndex < 0 || metadataIndex > kMaximumMetadataIndex || providerTable == nullptr) {
        log_suppressed(manager, key, metadataIndex, providerTable, package);
        return;
    }

    original(manager, key);
}

[[nodiscard]] std::byte* provider_consumer_target() noexcept {
    auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if (image == nullptr) {
        return nullptr;
    }
    std::byte* const target = image + kProviderConsumerRva;
    for (std::size_t index = 0; index < kProviderConsumerPrefix.size(); ++index) {
        if (target[index] != kProviderConsumerPrefix[index]) {
            return nullptr;
        }
    }
    return target;
}

} // namespace

bool install_activity_provider_stale_mapping_guard() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    std::byte* const target = provider_consumer_target();
    if (target == nullptr
        || !hooking::detour::install({target, reinterpret_cast<void*>(&provider_consumer)},
                                     g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=activity_provider_stale_mapping_guard "
                         "result=install_fail");
        return false;
    }
    g_original.store(reinterpret_cast<ProviderConsumer>(g_handle.original),
                     std::memory_order_release);
    g_suppressed.store(0U, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=activity_provider_stale_mapping_guard result=install_ok "
                     "scope=forced_towerfall invalid_index=skip");
    return true;
}

void uninstall_activity_provider_stale_mapping_guard() noexcept {
    if (!g_installed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    (void)hooking::detour::uninstall(g_handle);
    g_original.store(nullptr, std::memory_order_release);
    g_suppressed.store(0U, std::memory_order_release);
    g_handle = {};
}

} // namespace dawn::client::hooks::bootflow
