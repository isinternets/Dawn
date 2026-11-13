#include "unlocks_runtime.h"

#include <Windows.h>

#include <algorithm>

#include "../persistence/persistence.h"

namespace dawn::state::unlocks {
namespace {

ScopedTable g_table{};
SRWLOCK g_lock{SRWLOCK_INIT};

} // namespace

/** Publishes the immutable unlock policy for this process. */
void publish(const Table& table) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_table = {};
    g_table.accountFlags = table.accountFlags;
    g_table.profileFlags = table.profileFlags;
    g_table.objectiveValues = table.objectiveValues;
    g_table.accountProgressions = table.accountProgressions;
    ReleaseSRWLockExclusive(&g_lock);
}

void publish(const ScopedTable& table) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_table = table;
    ReleaseSRWLockExclusive(&g_lock);
}

ScopedTable snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    const ScopedTable copy = g_table;
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

bool character_snapshot(std::uint64_t characterSoid, CharacterTable& output) noexcept {
    output = {};
    if (characterSoid == 0) return false;
    AcquireSRWLockShared(&g_lock);
    const bool found = find_character(g_table, characterSoid, output);
    ReleaseSRWLockShared(&g_lock);
    return found;
}

namespace {

[[nodiscard]] persistence::Scope persistent_scope(Scope scope) noexcept {
    switch (scope) {
    case Scope::account: return persistence::Scope::account;
    case Scope::profile: return persistence::Scope::profile;
    case Scope::character: return persistence::Scope::character;
    case Scope::characterObject: return persistence::Scope::characterObject;
    }
    return persistence::Scope::account;
}

[[nodiscard]] CharacterTable* character_locked(std::uint64_t ownerSoid) noexcept {
    const std::size_t count = (std::min)(g_table.characterCount, g_table.characters.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (g_table.characters[index].characterSoid == ownerSoid) return &g_table.characters[index];
    }
    return nullptr;
}

} // namespace

bool set_flag(Scope scope,
              std::uint64_t ownerSoid,
              std::uint32_t slot,
              std::uint8_t value) noexcept {
    if (ownerSoid == 0 || value > kFlagSet) return false;
    AcquireSRWLockExclusive(&g_lock);
    std::uint8_t* target = nullptr;
    if (scope == Scope::account && ownerSoid == g_table.accountSoid
        && slot < g_table.accountFlags.size()) {
        target = &g_table.accountFlags[slot];
    } else if (scope == Scope::profile && ownerSoid == g_table.accountSoid
               && slot < g_table.profileFlags.size()) {
        target = &g_table.profileFlags[slot];
    } else if (CharacterTable* character = character_locked(ownerSoid)) {
        if (scope == Scope::character && slot < character->flags.size()) {
            target = &character->flags[slot];
        } else if (scope == Scope::characterObject && slot < character->objectFlags.size()) {
            target = &character->objectFlags[slot];
        }
    }
    const bool stored = target != nullptr
        && persistence::store_flag(persistent_scope(scope), ownerSoid, slot, value);
    if (stored) *target = value;
    ReleaseSRWLockExclusive(&g_lock);
    return stored;
}

bool set_objective(Scope scope,
                   std::uint64_t ownerSoid,
                   std::uint32_t slot,
                   std::int32_t value) noexcept {
    if (ownerSoid == 0) return false;
    AcquireSRWLockExclusive(&g_lock);
    std::int32_t* target = nullptr;
    if (scope == Scope::account && ownerSoid == g_table.accountSoid
        && slot < g_table.objectiveValues.size()) {
        target = &g_table.objectiveValues[slot];
    } else if (scope == Scope::characterObject) {
        CharacterTable* character = character_locked(ownerSoid);
        if (character != nullptr && slot < character->objectValues.size()) {
            target = &character->objectValues[slot];
        }
    }
    const bool stored = target != nullptr
        && persistence::store_objective(persistent_scope(scope), ownerSoid, slot, value);
    if (stored) *target = value;
    ReleaseSRWLockExclusive(&g_lock);
    return stored;
}

bool set_progression(Scope scope,
                     std::uint64_t ownerSoid,
                     std::uint32_t definitionIndex,
                     std::uint8_t lane,
                     std::int32_t value) noexcept {
    if (ownerSoid == 0 || definitionIndex >= build_data::progressions::kDefinitionCapacity
        || lane >= kProgressionLaneCount) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    std::int32_t* target = nullptr;
    if (scope == Scope::account && ownerSoid == g_table.accountSoid) {
        target = &g_table.accountProgressions[definitionIndex][lane];
    } else if (scope == Scope::character) {
        CharacterTable* character = character_locked(ownerSoid);
        if (character != nullptr) target = &character->progressions[definitionIndex][lane];
    }
    const bool stored = target != nullptr
        && persistence::store_progression(
            persistent_scope(scope), ownerSoid, definitionIndex, lane, value);
    if (stored) *target = value;
    ReleaseSRWLockExclusive(&g_lock);
    return stored;
}

bool rebind_account(const AccountState& before, const AccountState& after) noexcept {
    if (before.characterCount != after.characterCount
        || before.characterCount > g_table.characters.size()) return false;
    AcquireSRWLockExclusive(&g_lock);
    bool matches = g_table.accountSoid == before.primarySoid
        && g_table.characterCount == before.characterCount;
    for (std::size_t index = 0; matches && index < before.characterCount; ++index) {
        matches = g_table.characters[index].characterSoid == before.characters[index].soid;
    }
    if (matches) {
        g_table.accountSoid = after.primarySoid;
        for (std::size_t index = 0; index < after.characterCount; ++index) {
            g_table.characters[index].characterSoid = after.characters[index].soid;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return matches;
}

bool append_character(const Table& seed, std::uint64_t characterSoid) noexcept {
    if (characterSoid == 0) return false;
    AcquireSRWLockExclusive(&g_lock);
    bool appended = g_table.characterCount < g_table.characters.size();
    for (std::size_t index = 0; appended && index < g_table.characterCount; ++index) {
        appended = g_table.characters[index].characterSoid != characterSoid;
    }
    if (appended) {
        CharacterTable& character = g_table.characters[g_table.characterCount++];
        character = {};
        character.characterSoid = characterSoid;
        character.flags = seed.characterFlags;
        character.objectFlags = seed.characterObjectFlags;
        character.objectValues = seed.characterObjectValues;
        character.progressions = seed.characterProgressions;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return appended;
}

bool remove_character(std::uint64_t characterSoid) noexcept {
    if (characterSoid == 0) return false;
    AcquireSRWLockExclusive(&g_lock);
    const std::size_t count = (std::min)(g_table.characterCount, g_table.characters.size());
    std::size_t found = count;
    for (std::size_t index = 0; index < count; ++index) {
        if (g_table.characters[index].characterSoid == characterSoid) {
            found = index;
            break;
        }
    }
    if (found < count) {
        for (std::size_t index = found; index + 1U < count; ++index) {
            g_table.characters[index] = g_table.characters[index + 1U];
        }
        g_table.characters[count - 1U] = {};
        --g_table.characterCount;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return found < count;
}

/** Restores the empty unlock policy. */
void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_table = ScopedTable{};
    ReleaseSRWLockExclusive(&g_lock);
}

ScopedTable expand(const Table& seed, const AccountState& account) noexcept {
    ScopedTable expanded{};
    expanded.accountSoid = account.primarySoid;
    expanded.accountFlags = seed.accountFlags;
    expanded.profileFlags = seed.profileFlags;
    expanded.objectiveValues = seed.objectiveValues;
    expanded.accountProgressions = seed.accountProgressions;
    expanded.characterCount = (std::min)(account.characterCount, expanded.characters.size());
    for (std::size_t index = 0; index < expanded.characterCount; ++index) {
        CharacterTable& character = expanded.characters[index];
        character.characterSoid = account.characters[index].soid;
        character.flags = seed.characterFlags;
        character.objectFlags = seed.characterObjectFlags;
        character.objectValues = seed.characterObjectValues;
        character.progressions = seed.characterProgressions;
    }
    return expanded;
}

bool find_character(const ScopedTable& table,
                    std::uint64_t characterSoid,
                    CharacterTable& output) noexcept {
    output = {};
    if (characterSoid == 0 || table.characterCount > table.characters.size()) return false;
    for (std::size_t index = 0; index < table.characterCount; ++index) {
        if (table.characters[index].characterSoid == characterSoid) {
            output = table.characters[index];
            return true;
        }
    }
    return false;
}

} // namespace dawn::state::unlocks
