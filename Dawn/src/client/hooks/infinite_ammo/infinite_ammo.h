#pragma once

namespace dawn::client::hooks::infinite_ammo {

/**
 * Attaches to the reserve, magazine and sword setters. The magazine amount is passed through, so
 * weapons still reload.
 * @return True when all three resolved and the detours attached.
 */
[[nodiscard]] bool install() noexcept;

/** Detaches every detour. */
void uninstall() noexcept;
} // namespace dawn::client::hooks::infinite_ammo
