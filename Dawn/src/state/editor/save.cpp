// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include "edit.h"
#include "ability_rows.h"
#include "../runtime/runtime.h"
#include "../runtime/storage/internal.h"
#include "../build_data/abilities/ability_bucket_catalog.h"
#include "../persistence/persistence.h"
#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../client/content/items/packages/internal.h"
#include "../../server/bap/runtime.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace dawn::state::editor {
namespace {
namespace packages = client::content::items::packages;
using Selection_t = build_data::abilities::Selection;

/** Slot 11 carries the subclass, which is the only equipment an ability row depends on. */
constexpr std::size_t kSubclassSlot = 11;
/** The stock subclasses' socket entry lists, whose every standard pick Dawn builds a row for at start. */
constexpr std::array<std::uint16_t, 9> kStockSubclassLists{1, 2, 3, 5, 6, 7, 9, 10, 11};
namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

/**
 * One restore point covers the whole editing session.
 * Applying is a live action a player repeats freely, so copying the player database on every apply
 * would stall the frame and bury the one image worth keeping: the account as it stood before any
 * editing. The copy is taken once and later applies reuse it.
 */
std::atomic_bool g_restorePointTaken{false};

/** @return The ability selection one character currently has. */
Selection_t ability_selection(const CharacterState& character) {
    return {character.movementAbilityEntry, character.grenadeAbilityEntry,
        character.superAbilityEntry, character.meleeAbilityEntry, character.classAbilityEntry};
}

/**
 * Adds one row to the set when an equal row is not already in it.
 * @return False only when the fixed row storage is full.
 */
bool add_unique(std::span<build_data::abilities::Definition> rows, std::size_t& count,
    const build_data::abilities::Definition& row) {
    for (std::size_t i = 0; i < count; ++i) {
        if (rows[i].socketEntryListIndex == row.socketEntryListIndex && rows[i].selection == row.selection) return true;
    }
    if (count == rows.size()) return false;
    rows[count++] = row;
    return true;
}

/** One subclass combination a character needs a row for. */
struct Need {
    /** The subclass's socket entry list, which keys its rows. */
    std::uint16_t list{};
    Selection_t selection{};
    /** The equipped subclass's row is required; a stowed one's is only built when it can be. */
    bool equipped{};
};

/**
 * Stowed combinations that could not be built, as a stowed subclass that does not offer the
 * character's picks cannot, so a later apply does not open the package files for them again.
 */
std::vector<std::pair<std::uint16_t, Selection_t>> g_unbuildable;

/**
 * Lists the combinations every character needs: its equipped subclass under its picks, and each
 * subclass it has stowed under the same picks. The game equips a stowed subclass without changing
 * the picks and draws the character from that subclass's row at once, so a row missing then ends
 * the session.
 * @param catalog The editor's catalog, which names the stowed subclasses, or null to list only the
 * equipped ones, as an equip made in game does: it runs where the catalog may not be loaded.
 * @return False when an equipped subclass is missing from the build.
 */
bool needed(const AccountState& account, const Catalog* catalog, std::vector<Need>& output) {
    output.clear();
    // Every equipped row comes before any stowed one, so the stowed ones never take the room an
    // equipped one needs.
    for (std::size_t i = 0; i < account.characterCount; ++i) {
        const CharacterState& character = account.characters[i];
        if (const auto& subclass = character.equipment.slots[kSubclassSlot]) {
            build_data::items::Definition definition{};
            build_data::items::details::Definition detail{};
            if (!build_data::find_item_definition_hash(subclass->definitionHash, definition)
                || !build_data::find_configured_item_detail(definition.definitionIndex, detail)) return false;
            output.push_back({detail.socketEntryListIndex, ability_selection(character), true});
        }
    }
    for (std::size_t i = 0; catalog != nullptr && i < account.characterCount; ++i) {
        const CharacterState& character = account.characters[i];
        const Selection_t selection = ability_selection(character);
        for (std::size_t j = 0; j < character.inventory.count; ++j) {
            const auto* item = catalog->find(character.inventory.values[j].definitionHash);
            if (!item || item->kind != GearKind::subclass) continue;
            const std::pair<std::uint16_t, Selection_t> key{item->detail.socketEntryListIndex, selection};
            if (std::find(g_unbuildable.begin(), g_unbuildable.end(), key) == g_unbuildable.end()) output.push_back({key.first, selection, false});
        }
    }
    return true;
}

/**
 * @return True when every needed subclass combination is already published.
 * Applying is now a per-edit action, so the common case must not open the package files at all.
 */
bool ability_rows_published(std::span<const Need> needs,
    std::span<build_data::abilities::Definition> rows, std::size_t& count) {
    for (const Need& need : needs) {
        build_data::abilities::Definition row{};
        if (!build_data::find_ability_buckets(need.list, need.selection, row)) return false;
        if (!add_unique(rows, count, row)) return false;
    }
    return true;
}

/** Builds the subclass ability combinations every character in the account needs. */
bool ability_rows(const AccountState& account, const Catalog* catalog,
    std::span<build_data::abilities::Definition> rows, std::size_t& count) {
    // Dawn's own rows for the stock subclasses are kept, and on top of them only the rows the account
    // needs now. Rows were once only ever added, and the build cache keeps them from one session to
    // the next, so every custom subclass and every pick ever tried stayed until the table was full.
    if (!build_data::abilities::snapshot(rows, count)) return false;
    count = static_cast<std::size_t>(std::remove_if(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(count),
        [](const build_data::abilities::Definition& row) {
            return std::find(kStockSubclassLists.begin(), kStockSubclassLists.end(), row.socketEntryListIndex) == kStockSubclassLists.end();
        }) - rows.begin());
    const std::size_t published = count;
    std::vector<Need> needs;
    if (!needed(account, catalog, needs)) return false;
    if (ability_rows_published(needs, rows, count)) return true;
    count = published;
    reader::BlockKeys keys{};
    auto scratch = std::make_unique<reader::Scratch>();
    struct Cleanup { reader::BlockKeys& keys; reader::Scratch& scratch;
        ~Cleanup() { reader::close_files(scratch); SecureZeroMemory(&keys, sizeof keys); } } cleanup{keys, *scratch};
    core::path::Buffer directory{};
    if (!packages::collect_keys(keys) || !packages::package_directory(directory)) return false;
    reader::Source source{directory.chars.data(), &keys};
    std::array<std::uint32_t, packages::kContainerCandidates> tags{};
    std::size_t tagCount{}; tables::Array array{};
    std::vector<std::byte> globals, root, table, definition, blob;
    bool found = false;
    if (!packages::investment_globals_tags(tags, tagCount)) return false;
    for (std::size_t i = 0; i < tagCount && !found; ++i) {
        std::uint32_t rootTag{}, tableTag{};
        found = reader::read_tag(source, *scratch, tags[i], globals) && tables::child_tag(globals, 0, rootTag)
            && reader::read_tag(source, *scratch, rootTag, root) && tables::slot_tag(root, 97, tableTag)
            && reader::read_tag(source, *scratch, tableTag, table) && tables::find_array_at(table, 8, array);
    }
    if (!found) return false;
    for (const Need& need : needs) {
        const std::uint16_t list = need.list;
        build_data::abilities::Definition row{};
        if (!build_data::find_ability_buckets(list, need.selection, row)) {
            tables::IndexRow index{};
            if (!tables::index_row(table, array, list, index)
                || !reader::read_tag(source, *scratch, index.targetTag, definition)
                || !packages::build_ability_buckets(source, *scratch, definition, blob, need.selection, row)) {
                if (need.equipped) return false;
                g_unbuildable.emplace_back(list, need.selection);
                continue;
            }
            row.socketEntryListIndex = list; row.selection = need.selection;
        }
        if (!add_unique(rows, count, row)) {
            if (need.equipped) return false;
            g_unbuildable.emplace_back(list, need.selection);
        }
    }
    return true;
}

/**
 * Checks that every character's equipment is one the installed build can carry.
 * The account is shared, so a character the editor never opened can still refuse the apply.
 * @param account Draft after-image to check.
 * @param catalog Loaded item catalog.
 * @param message Receives the reason when a character is refused.
 * @return True when every character resolves through its own selected-character loadout.
 */
bool every_character_resolves(const AccountState& account, const Catalog& catalog, std::string& message) {
    auto validation = std::make_unique<AccountState>(account);
    auto resolved = std::make_unique<middleware::datagen::family4::loadout::ResolvedLoadout>();
    for (std::size_t c = 0; c < account.characterCount; ++c) {
        const auto& character = account.characters[c];
        for (std::size_t slot = 0; slot < character.equipment.slots.size(); ++slot) {
            const auto& item = character.equipment.slots[slot];
            if (!item) continue;
            const auto* definition = catalog.find(item->definitionHash);
            if (!definition || definition->slot != slot) {
                message = "Equipped gear must match its equipment slot."; return false;
            }
        }
        // Each character encodes as the selected one, which is the only form the resolver accepts.
        for (std::size_t i = 0; i < validation->characterCount; ++i) validation->characters[i].selected = i == c;
        if (!middleware::datagen::family4::loadout::resolve(*validation, c, *resolved)) {
            message = "The loadout does not fit the game's inventory or socket layout. Check your changes."; return false;
        }
    }
    return true;
}
}

bool publish_ability_rows(const AccountState& account) noexcept {
    try {
        std::vector<Need> needs;
        if (!needed(account, nullptr, needs)) return false;
        // The common equip has its row already, and must not open the package files or rewrite
        // the published rows.
        if (std::all_of(needs.begin(), needs.end(), [](const Need& need) {
                build_data::abilities::Definition row{};
                return build_data::find_ability_buckets(need.list, need.selection, row);
            })) return true;
        std::vector<build_data::abilities::Definition> rows(build_data::abilities::kDefinitionCapacity);
        std::size_t count{};
        return ability_rows(account, nullptr, rows, count) && build_data::publish_ability_buckets(std::span(rows).first(count));
    } catch (...) { return false; }
}
bool apply(Draft& draft, const Catalog& catalog, std::string& message, bool& live) {
    live = false;
    if (!draft.dirty) { message = "No changes to apply."; return false; }
    auto prepared = std::make_unique<AccountState>(draft.after);
    if (!prepare_commit(draft, catalog, *prepared, message)) return false;
    if (!every_character_resolves(*prepared, catalog, message)) return false;
    std::vector<build_data::abilities::Definition> abilities(build_data::abilities::kDefinitionCapacity);
    std::size_t abilityCount{};
    if (!ability_rows(*prepared, &catalog, abilities, abilityCount)) {
        message = "The selected subclass abilities could not be resolved."; return false;
    }
    // A draft the game has moved the account past is refused before anything is written, the restore
    // point included. The commit below checks again under the lock, which is what makes it safe.
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const bool stale = runtime::storage::g_state.account != draft.before;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (stale) {
        message = "Account changed in game. Reload, then apply again. Edits kept.";
        return false;
    }
    // The subclass rows go out before the account does. The game encodes a character's abilities
    // from these rows, so an account committed first could be encoded without its row, and a
    // publish that failed after the commit left the character unable to encode at all. Rows are
    // only ever added, so publishing them for a commit that is then refused changes nothing.
    if (!build_data::publish_ability_buckets(std::span(abilities).first(abilityCount))) {
        message = "Couldn't publish the subclass abilities. Nothing applied."; return false;
    }
    for (std::size_t i = 0; i < prepared->characterCount; ++i) {
        const auto& subclass = prepared->characters[i].equipment.slots[kSubclassSlot];
        const auto* item = subclass ? catalog.find(subclass->definitionHash) : nullptr;
        build_data::abilities::Definition row{};
        if (item != nullptr && !build_data::find_ability_buckets(item->detail.socketEntryListIndex,
                                                                ability_selection(prepared->characters[i]), row)) {
            message = "A subclass ability has no published row. Nothing applied."; return false;
        }
    }
    // One restore point covers the whole session; later applies reuse the image taken here.
    const bool firstApply = !g_restorePointTaken.load(std::memory_order_acquire);
    if (firstApply) {
        if (!persistence::backup_for_editor()) {
            message = "Could not create the save backup. Your account was not changed."; return false;
        }
        g_restorePointTaken.store(true, std::memory_order_release);
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto& account = runtime::storage::g_state.account;
    bool committed = false;
    if (account != draft.before) {
        message = "Account changed in game. Reload, then apply again. Edits kept.";
    } else if (!persistence::commit_account(account, *prepared)) {
        message = "Could not commit this change. Your account was not changed.";
    } else {
        account = *prepared;
        draft.after = draft.before = *prepared;
        draft.dirty = false;
        committed = true;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    if (!committed) return false;

    // Every peer holding the account rebuilds its inventory, appearance and roster from the
    // committed state on its next service poll, so the change shows in game with no restart.
    live = server::bap::publish_external_account_mutation() != 0;
    if (firstApply) {
        message = live ? "Applied. Backup in Dawn/editor-backups."
                       : "Saved for next sign-in. Backup in Dawn/editor-backups.";
    } else {
        message = live ? "Applied." : "Saved for next sign-in.";
    }
    return true;
}

bool republish() noexcept {
    return server::bap::publish_external_account_mutation() != 0;
}
}
