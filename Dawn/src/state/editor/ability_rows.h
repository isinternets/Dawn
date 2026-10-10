// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../account/account_state.h"

namespace dawn::state::editor {
/**
 * Publishes the ability row each character's equipped subclass needs under its picks, building any
 * the build lacks: Dawn builds rows at launch only for the stock subclasses and those equipped then,
 * so an authored subclass equipped in game later would have none and could not be encoded.
 * Does nothing when every row is in.
 * @return False when a row could not be built or published.
 */
[[nodiscard]] bool publish_ability_rows(const AccountState& account) noexcept;
}