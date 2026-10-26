#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/runtime/runtime.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
namespace {

/**
 * The character sign-in step's enter handler. Anchored on its two setup stores, which write the
 * step's latch field and the paired setup words, so it cannot match a sibling step's handler.
 */
constexpr std::string_view kEnterSignatureText =
    "40 53 48 83 EC ? 48 8B D9 C7 41 38 FF FF FF FF 66 C7 41 3C 00 00 33 D2";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kEnterSignature =
    signature<signature_length(kEnterSignatureText)>(kEnterSignatureText);

/** Fields of the boot step this hook touches, as byte offsets from the step's own base. */
struct StepLayout {
    /**
     * The stay flag. The exit gate needs it non-zero to stay on the select screen. The enter
     * handler clears it, so the step would skip selection on its first frame.
     */
    static constexpr std::size_t stayFlag = 72;
};

/** Value the step's own listener writes to hold the screen. Matched here. */
constexpr std::uint8_t kHold = 1;

using EnterHandler = void(__fastcall*)(std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<EnterHandler> g_original{nullptr};
std::atomic_bool g_reported{false};
/** The step the Client last entered, which is the one it is in whenever it asks for a release. */
std::atomic<std::byte*> g_step{nullptr};
/** True from the request until the release is applied, or until the step is entered again. */
std::atomic_bool g_releasePending{false};

/**
 * Writes the stay flag of one step.
 * Guarded, so a fault costs the release and not the process.
 * @param step Borrowed boot-step base pointer.
 * @param value Flag value to write.
 * @return True when the byte was written.
 */
[[nodiscard]] bool write_stay_flag(std::byte* step, std::uint8_t value) noexcept {
    __try {
        *reinterpret_cast<volatile std::uint8_t*>(step + StepLayout::stayFlag) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/**
 * Holds the character-select screen after the step sets itself up.
 * Runs each time the Client enters the step, on the boot-step thread, after the original writes
 * its defaults, so the original cannot overwrite this write.
 * @param step Borrowed boot-step base pointer.
 */
__declspec(noinline) void __fastcall enter_handler(std::byte* step) noexcept {
    const EnterHandler original = g_original.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(step);
    }
    if (step == nullptr) {
        return;
    }
    g_step.store(step, std::memory_order_release);
    g_releasePending.store(false, std::memory_order_release);
    // An account with no character goes to creating one, which the retail step already handles: it
    // leaves the screen on its own once that character exists. Holding the screen there would keep
    // it up after the creation, so the hold only applies once there is a character to select.
    if (state::account_character_count() == 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=bootflow stage=character_select result=released reason=no_character");
        return;
    }
    std::memcpy(step + StepLayout::stayFlag, &kHold, sizeof kHold);
    if (!g_reported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=bootflow stage=character_select result=held");
    }
}

} // namespace

/**
 * Attaches the character-select hold.
 * @return True when the target is found and the detour attaches.
 */
bool install_character_select_hold() noexcept {
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kEnterSignature, "character_signin_enter");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=character_select result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&enter_handler)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=character_select result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<EnterHandler>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=character_select result=ok");
    return true;
}

/** Detaches the character-select hold. */
void uninstall_character_select_hold() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_step.store(nullptr, std::memory_order_release);
    g_releasePending.store(false, std::memory_order_release);
    g_reported.store(false, std::memory_order_release);
}

/** Asks for the hold to be released at the next request. */
void request_character_select_release() noexcept {
    g_releasePending.store(true, std::memory_order_release);
}

/** Releases the hold when one was requested. */
void apply_character_select_release() noexcept {
    if (!g_releasePending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    std::byte* const step = g_step.load(std::memory_order_acquire);
    const bool released = step != nullptr && write_stay_flag(step, 0);
    core::log::write(core::log::Channel::client,
                     released ? core::log::Level::info : core::log::Level::warn,
                     released ? "ev=bootflow stage=character_select result=released reason=created"
                              : "ev=bootflow stage=character_select result=release_failed");
}

} // namespace dawn::client::hooks::bootflow
