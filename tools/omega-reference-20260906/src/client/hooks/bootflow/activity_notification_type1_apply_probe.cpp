#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/activity/forced/activity_forced_destination.h"
#include "../../hooking/detour.h"
#include "internal.h"
#include "legacy_owner_sentinel.h"

namespace dawn::client::hooks::bootflow {
namespace {

/** Event-25 client consumer reached by activity notification type 1. */
constexpr std::uintptr_t kTypeOneClientApplyRva = 0x003CB4A0U;
constexpr std::array<std::byte, 21> kTypeOneClientApplyPrefix{
    std::byte{0x40}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}, std::byte{0xEC},
    std::byte{0x20}, std::byte{0x80}, std::byte{0x79}, std::byte{0x20}, std::byte{0x00},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0xFA}, std::byte{0x48}, std::byte{0x89},
    std::byte{0x74}, std::byte{0x24}, std::byte{0x40}, std::byte{0x48}, std::byte{0x8B},
    std::byte{0xF1}};

/** The native apply copies this decoded payload into the activity client's pending slot. */
constexpr std::size_t kActivityClientContextOffset = 0x10U;
constexpr std::size_t kActivityClientOnlineOffset = 0x20U;
constexpr std::size_t kActivityClientStateOffset = 0x111E4U;
constexpr std::size_t kPendingReadyOffset = 0x592E0U;
constexpr std::size_t kPendingPayloadOffset = 0x592E8U;
constexpr std::size_t kDecodedPayloadBytes = 0x650U;
constexpr std::size_t kDecodedActivityIndexOffset = 0x6CU;
using TypeOneClientApply = void(__fastcall*)(std::byte* activityClient,
                                             const std::byte* decoded) noexcept;

hooking::detour::Handle g_handle{};
std::atomic<TypeOneClientApply> g_original{nullptr};
std::atomic_uint32_t g_observed{};
std::atomic_int g_previousAfterReady{-1};

[[nodiscard]] bool opening_forced_destination(std::string_view& package) noexcept {
    static thread_local state::activity::forced::ForcedDestination forced{};
    state::activity::forced::snapshot(forced);
    package = std::string_view(forced.packageName.data(), forced.packageNameLength);
    return state::activity::forced::override_active()
           && (package == "cine_110_twr" || package == "mission_towerfall");
}

template <typename Value>
[[nodiscard]] Value read_value(const std::byte* source) noexcept {
    Value value{};
    if (source != nullptr) {
        std::memcpy(&value, source, sizeof value);
    }
    return value;
}

[[nodiscard]] std::uint64_t hash_payload(const std::byte* source) noexcept {
    if (source == nullptr) {
        return 0;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < kDecodedPayloadBytes; ++index) {
        hash ^= std::to_integer<std::uint8_t>(source[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::byte* target() noexcept {
    auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if (image == nullptr) {
        return nullptr;
    }
    std::byte* const candidate = image + kTypeOneClientApplyRva;
    for (std::size_t index = 0; index < kTypeOneClientApplyPrefix.size(); ++index) {
        if (candidate[index] != kTypeOneClientApplyPrefix[index]) {
            return nullptr;
        }
    }
    return candidate;
}

/**
 * Observes the authentic type-1 handoff without changing its client, decoded body, or pending
 * state. Repeated global-state pushes reveal whether the previous pending value was consumed.
 */
__declspec(noinline) void __fastcall type_one_client_apply(std::byte* activityClient,
                                                            const std::byte* decoded) noexcept {
    std::byte* const contextBefore =
        activityClient != nullptr
            ? read_value<std::byte*>(activityClient + kActivityClientContextOffset)
            : nullptr;
    const int readyBefore =
        contextBefore != nullptr
            ? static_cast<int>(read_value<std::uint8_t>(contextBefore + kPendingReadyOffset))
            : -1;
    const std::uint16_t incomingActivity =
        decoded != nullptr
            ? read_value<std::uint16_t>(decoded + kDecodedActivityIndexOffset)
            : UINT16_MAX;
    const std::uint64_t incomingHash = hash_payload(decoded);
    const std::uint8_t clientOnline =
        activityClient != nullptr
            ? read_value<std::uint8_t>(activityClient + kActivityClientOnlineOffset)
            : 0;
    const std::int32_t clientState =
        activityClient != nullptr
            ? read_value<std::int32_t>(activityClient + kActivityClientStateOffset)
            : -1;

    const TypeOneClientApply original = g_original.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(activityClient, decoded);
    }

    std::byte* const contextAfter =
        activityClient != nullptr
            ? read_value<std::byte*>(activityClient + kActivityClientContextOffset)
            : nullptr;
    const int readyAfter =
        contextAfter != nullptr
            ? static_cast<int>(read_value<std::uint8_t>(contextAfter + kPendingReadyOffset))
            : -1;
    const std::byte* const stored =
        contextAfter != nullptr ? contextAfter + kPendingPayloadOffset : nullptr;
    const std::uint16_t storedActivity =
        stored != nullptr
            ? read_value<std::uint16_t>(stored + kDecodedActivityIndexOffset)
            : UINT16_MAX;
    const std::uint64_t storedHash = hash_payload(stored);
    const int previousAfter =
        g_previousAfterReady.exchange(readyAfter, std::memory_order_acq_rel);
    const bool consumedSincePrevious = previousAfter == 1 && readyBefore == 0;

    std::string_view package{};
    const bool opening = opening_forced_destination(package);
    const std::uint32_t observation =
        g_observed.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (!opening && observation > 16U) {
        return;
    }

    std::array<char, 640> line{};
    const int length = std::snprintf(
        line.data(),
        line.size(),
        "ev=bootflow stage=activity_notification_type1_client_apply n=%u activity_client=%p context_before=%p context_after=%p client_online=%u client_state=%d ready_before=%d ready_after=%d previous_after=%d consumed_since_previous=%u backlogged_before=%u incoming_activity=%u stored_activity=%u incoming_hash=0x%llX stored_hash=0x%llX identical=%u opening=%u package=%.*s mutation=observe_only",
        observation,
        static_cast<void*>(activityClient),
        static_cast<void*>(contextBefore),
        static_cast<void*>(contextAfter),
        static_cast<unsigned>(clientOnline),
        clientState,
        readyBefore,
        readyAfter,
        previousAfter,
        consumedSincePrevious ? 1U : 0U,
        readyBefore == 1 ? 1U : 0U,
        static_cast<unsigned>(incomingActivity),
        static_cast<unsigned>(storedActivity),
        static_cast<unsigned long long>(incomingHash),
        static_cast<unsigned long long>(storedHash),
        incomingHash != 0 && incomingHash == storedHash ? 1U : 0U,
        opening ? 1U : 0U,
        static_cast<int>(package.size()),
        package.data());
    if (length > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(length)});
    }
}

[[nodiscard]] std::array<legacy_owner_sentinel::HookOwnership, 1>
activity_notification_type1_hook_ownership() noexcept {
    // LEGACY_OWNER_SENTINEL_BEGIN(activity_notification_type1, 1)
    return {{{g_handle.attached,
              g_original.load(std::memory_order_acquire) != nullptr}}};
    // LEGACY_OWNER_SENTINEL_END(activity_notification_type1)
}

} // namespace

bool activity_notification_type1_apply_probe_has_ownership() noexcept {
    return legacy_owner_sentinel::has_ownership(
        activity_notification_type1_hook_ownership());
}

bool install_activity_notification_type1_apply_probe() noexcept {
    if (g_handle.attached) {
        return true;
    }
    std::byte* const applyTarget = target();
    if (applyTarget == nullptr) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::warn,
            "ev=bootflow stage=activity_notification_type1_client_apply_probe result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{applyTarget,
                                     reinterpret_cast<void*>(&type_one_client_apply)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::warn,
            "ev=bootflow stage=activity_notification_type1_client_apply_probe result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<TypeOneClientApply>(g_handle.original),
                     std::memory_order_release);
    core::log::write(
        core::log::Channel::client,
        core::log::Level::info,
        "ev=bootflow stage=activity_notification_type1_client_apply_probe result=ok mode=observe target_rva=0x3CB4A0 pending_ready_offset=0x592E0 pending_payload_offset=0x592E8");
    return true;
}

void uninstall_activity_notification_type1_apply_probe() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_observed.store(0, std::memory_order_release);
    g_previousAfterReady.store(-1, std::memory_order_release);
}

} // namespace dawn::client::hooks::bootflow
