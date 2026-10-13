#pragma once

#include <cstdint>

#include "../account/account_state.h"
#include "../investment/investment.h"
#include "../unlocks/definition.h"

namespace dawn::state::persistence {

/** Durable progression scopes. The owner is the account or character SOID. */
enum class Scope : std::uint8_t {
    account,
    profile,
    character,
    characterObject,
};

/** One durable mission checkpoint record. Runtime encounter state is deliberately absent. */
struct MissionRecord {
    std::uint64_t characterSoid{};
    std::uint32_t missionHash{};
    std::uint32_t checkpointHash{};
    std::int32_t checkpointSliceSet{};
    std::int32_t activityIndex{};
    std::int32_t progress{};
    std::int64_t updatedUtc{};
    bool completed{};
};

/** One server-authored reward which remains pending until its account change commits. */
struct RewardDebt {
    std::uint64_t debtId{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t runtimeEpoch{};
    std::uint64_t sessionId{};
    std::uint64_t runId{};
    std::uint32_t missionHash{};
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
    std::int32_t credited{};
    bool delivered{};
};

/**
 * Opens the module-local player-state database. A new database imports legacyAccount exactly
 * once; an existing database is authoritative. A null module explicitly selects memory-only
 * operation for unit tests and non-game hosts.
 */
[[nodiscard]] bool initialize(void* module,
                              const AccountState& legacyAccount,
                              const unlocks::Table& legacyUnlocks,
                              const Family5State& legacyFamily5,
                              AccountState& loadedAccount,
                              unlocks::ScopedTable& loadedUnlocks,
                              Family5State& loadedFamily5) noexcept;

/** Closes the database and clears cached revision/allocator state. */
void shutdown() noexcept;

/** True when a durable database is open rather than the explicit memory-only mode. */
[[nodiscard]] bool enabled() noexcept;

/** Makes a consistent SQLite backup before an editor save. Uses SQLite's online backup API. */
[[nodiscard]] bool backup_for_editor() noexcept;

/**
 * Durably replaces the normalized account snapshot under a database revision guard. The caller
 * must publish `after` to process memory only after this succeeds.
 */
[[nodiscard]] bool commit_account(const AccountState& before,
                                  const AccountState& after) noexcept;

/**
 * Durably replaces the account snapshot with one that has lost a single character, and drops every
 * durable row the removed character owned. `after` must hold the other characters unchanged and in
 * order. The caller must publish `after` to process memory only after this succeeds.
 */
[[nodiscard]] bool commit_character_removal(const AccountState& before,
                                            const AccountState& after,
                                            std::uint64_t removedSoid) noexcept;

/** Commit menu preferences and advance the account revision without rewriting inventory. */
[[nodiscard]] bool commit_settings(const account::settings::AccountSettings& settings) noexcept;

/** Durable, non-recycling instance identity candidates. */
[[nodiscard]] bool next_item_instance_soid(const AccountState& account,
                                           std::uint64_t& output) noexcept;
[[nodiscard]] bool next_profile_item_instance_soid(const AccountState& account,
                                                   std::uint64_t& output) noexcept;

/** Sparse durable unlock/progression accessors. Missing rows are reported through `found`. */
[[nodiscard]] bool load_flag(Scope scope,
                             std::uint64_t ownerSoid,
                             std::uint32_t slot,
                             bool& found,
                             std::uint8_t& value) noexcept;
[[nodiscard]] bool store_flag(Scope scope,
                              std::uint64_t ownerSoid,
                              std::uint32_t slot,
                              std::uint8_t value) noexcept;
[[nodiscard]] bool load_objective(Scope scope,
                                  std::uint64_t ownerSoid,
                                  std::uint32_t slot,
                                  bool& found,
                                  std::int32_t& value) noexcept;
[[nodiscard]] bool store_objective(Scope scope,
                                   std::uint64_t ownerSoid,
                                   std::uint32_t slot,
                                   std::int32_t value) noexcept;
[[nodiscard]] bool load_progression(Scope scope,
                                    std::uint64_t ownerSoid,
                                    std::uint32_t definitionIndex,
                                    std::uint8_t lane,
                                    bool& found,
                                    std::int32_t& value) noexcept;
[[nodiscard]] bool store_progression(Scope scope,
                                     std::uint64_t ownerSoid,
                                     std::uint32_t definitionIndex,
                                     std::uint8_t lane,
                                     std::int32_t value) noexcept;

[[nodiscard]] bool load_mission(std::uint64_t characterSoid,
                                std::uint32_t missionHash,
                                bool& found,
                                MissionRecord& record) noexcept;
[[nodiscard]] bool store_mission(const MissionRecord& record) noexcept;

/** Atomically commits a reward/account change and its mission checkpoint/completion. */
[[nodiscard]] bool commit_account_and_mission(const AccountState& before,
                                              const AccountState& after,
                                              const MissionRecord& record) noexcept;

/**
 * Creates an idempotent pending completion reward and marks its already-observed mission complete
 * in the same transaction. Repeated callbacks for the same account/session/run/definition return
 * the original row through `debt`.
 */
[[nodiscard]] bool offer_reward(std::uint64_t accountSoid,
                                std::uint64_t characterSoid,
                                std::uint64_t sessionId,
                                std::uint64_t runId,
                                std::uint32_t missionHash,
                                std::uint32_t definitionHash,
                                std::int32_t quantity,
                                std::int64_t updatedUtc,
                                RewardDebt& debt) noexcept;

/** Loads the oldest undelivered debt for the authenticated account. */
[[nodiscard]] bool load_pending_reward(std::uint64_t accountSoid,
                                       bool& found,
                                       RewardDebt& debt) noexcept;

/** Marks a capped/no-credit debt delivered without changing the account. */
[[nodiscard]] bool finish_reward(std::uint64_t debtId, std::int32_t credited) noexcept;

/** Atomically commits one reward account after-image and marks its exact debt delivered. */
[[nodiscard]] bool commit_account_and_reward(const AccountState& before,
                                             const AccountState& after,
                                             std::uint64_t debtId,
                                             std::int32_t credited) noexcept;

} // namespace dawn::state::persistence
