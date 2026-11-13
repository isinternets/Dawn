#pragma once

#include "definition.h"

namespace dawn::state::unlocks {

/** Durable ownership scopes exposed to verified progression writers. */
enum class Scope : std::uint8_t { account, profile, character, characterObject };

/**
 * Publishes the immutable unlock policy for this process.
 * @param table Complete authored policy.
 */
void publish(const Table& table) noexcept;

/** Publishes fully scoped durable unlock state. */
void publish(const ScopedTable& table) noexcept;

/** @return The active unlock policy, or an empty policy when none was published. */
[[nodiscard]] ScopedTable snapshot() noexcept;

/** Copies one exact character's banks. */
[[nodiscard]] bool character_snapshot(std::uint64_t characterSoid,
                                      CharacterTable& output) noexcept;

/** Persists and publishes one validated flag mutation. */
[[nodiscard]] bool set_flag(Scope scope,
                            std::uint64_t ownerSoid,
                            std::uint32_t slot,
                            std::uint8_t value) noexcept;

/** Persists and publishes one validated objective mutation. */
[[nodiscard]] bool set_objective(Scope scope,
                                 std::uint64_t ownerSoid,
                                 std::uint32_t slot,
                                 std::int32_t value) noexcept;

/** Persists and publishes one validated progression-lane mutation. */
[[nodiscard]] bool set_progression(Scope scope,
                                   std::uint64_t ownerSoid,
                                   std::uint32_t definitionIndex,
                                   std::uint8_t lane,
                                   std::int32_t value) noexcept;

/** Rebinds cached account and character owners after their durable SOIDs migrate. */
[[nodiscard]] bool rebind_account(const AccountState& before,
                                  const AccountState& after) noexcept;

/**
 * Adds the banks of one newly created character, started from the authored policy.
 * Nothing is stored durably: a character without rows reads its policy, exactly as one loaded
 * from a database that holds none does.
 * @param seed Authored policy every character's banks start from.
 * @param characterSoid Key of the new character.
 * @return False when the key is zero, the table has no free slot, or it already holds the key.
 */
[[nodiscard]] bool append_character(const Table& seed, std::uint64_t characterSoid) noexcept;

/**
 * Drops one character's unlock table and closes the gap, so the rest keep the order the account
 * lists them in, which `rebind_account` relies on.
 * @return False when the table holds no such character.
 */
[[nodiscard]] bool remove_character(std::uint64_t characterSoid) noexcept;

/** Restores the empty unlock policy. */
void clear() noexcept;

} // namespace dawn::state::unlocks
