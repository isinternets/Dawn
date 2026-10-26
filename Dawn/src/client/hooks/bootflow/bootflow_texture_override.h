#pragma once

namespace dawn::client::hooks::bootflow::texture_override {
/**
 * Replaces selected decoded bootflow texture entries by TagHash with DDS data embedded in Dawn.
 * The stock package remains registered and unchanged.
 * @param module Dawn DLL module that owns the RCDATA resources.
 * @return True while the native GPU-resource callback detour is attached.
 */
[[nodiscard]] bool install(void* module) noexcept;

/** @return True when the callback detour is detached. */
[[nodiscard]] bool uninstall() noexcept;

/** @return True while decoded bootflow texture entries are being overridden. */
[[nodiscard]] bool is_installed() noexcept;

} // namespace dawn::client::hooks::bootflow::texture_override
