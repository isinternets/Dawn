// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../../scaling/dpi/ui_dpi_scaling.h"
#include "../art.h"
#include "../controls.h"
#include "../internal.h"
#include "../presets.h"
#include "../tooltip.h"

namespace dawn::core::ui::modules::loadout::internal {
namespace {

using scaling::dpi::pixels;
namespace inv = state::account::inventory;

/** Popup id of the sheet, used by both the action that opens it and the sheet itself. */
constexpr const char* kLoadoutsTitle = "Saved Loadouts";
/** Id of the menu the Copy To action opens, and the payload a row carries while it is dragged. */
constexpr const char* kCopyMenu = "copy_to";
constexpr const char* kRowPayload = "DAWN_LOADOUT_ROW";
/**
 * One loadout row, laid out as the game previews a loadout: the subclass stands as its emblem,
 * the name in the title cut beside it, and under the name the pieces it puts on, weapons then
 * armor, each framed in its rarity as the cards frame them. Every row is the same height, so
 * choosing one never moves another under the pointer.
 */
constexpr float kRowPadding = 8.0F;
constexpr float kEmblemExtent = 46.0F;
constexpr float kEmblemGap = 12.0F;
constexpr float kPieceExtent = 24.0F;
constexpr float kPieceGap = 3.0F;
constexpr float kPieceGroupGap = 10.0F;
constexpr float kLineGap = 5.0F;
/** Gap between the name and the words set at its line's far end. */
constexpr float kWordGap = 10.0F;
/** The share of its line a row's name always keeps. The words at the far end are cut to the rest. */
constexpr float kNameShare = 0.5F;
/**
 * The strip under the list, which holds the chosen loadout's armor totals and its actions in one
 * place whichever row is chosen: its name's share of the first line, and the gap over the actions.
 */
constexpr float kStripNameShare = 0.45F;
constexpr float kDetailGap = 8.0F;
constexpr float kActionGap = 6.0F;
/**
 * A button asking to be pressed again is filled with the colour its action deserves at these
 * strengths, at rest, under the pointer and pressed, with its label left in full. Coloured text on a
 * lit button fell to 2.3:1 while pressed; full text on the tint stays above 5:1 in every state.
 */
constexpr float kAskingFill = 0.22F;
constexpr float kAskingFillHovered = 0.32F;
constexpr float kAskingFillActive = 0.42F;
/** A piece no longer available is veiled rather than left out, so the loadout still reads whole. */
constexpr ImVec4 kUnavailableVeil{0.0F, 0.0F, 0.0F, 0.62F};
/**
 * The chosen loadout's actions. Save Over and Delete each ask again, in place, before they act, and
 * while they ask, their colour and what they say under the pointer tell the two apart.
 */
constexpr const char* kEquipLabel = "Equip";
constexpr const char* kEquippedLabel = "Equipped";
constexpr const char* kSaveOverLabel = "Save Over";
constexpr const char* kSaveConfirmLabel = "Confirm";
constexpr const char* kRenameLabel = "Rename";
constexpr const char* kCopyLabel = "Copy To";
constexpr const char* kDeleteLabel = "Delete";
constexpr const char* kDeleteConfirmLabel = "Confirm";
/** What each action does, under the pointer. */
constexpr const char* kEquipTip = "Shortcut: Enter or double-click.";
constexpr const char* kInPlaceTip = "Already equipped.";
constexpr const char* kSaveOverTip = "Replace with what's equipped now.";
constexpr const char* kSaveConfirmTip = "Click again to save over this loadout. Shortcut: Enter.";
constexpr const char* kRenameTip = "Shortcut: F2.";
constexpr const char* kCopyTip = "Copy to another character of this class.";
constexpr const char* kDeleteTip = "Shortcut: Delete.";
constexpr const char* kDeleteConfirmTip = "Click again to delete this loadout. Shortcut: Enter.";
/** Said in the strip while no loadout is chosen. */
constexpr const char* kChooseHint = "Select a loadout to equip, rename or delete it.";
/** The row's own words, set at the far end of its name line, and the one said in full under the pointer. */
constexpr const char* kInPlaceWord = "Equipped";
constexpr const char* kPastWord = "Unclaimed";
constexpr const char* kPastTip = "From a deleted character. Using it makes it this character's.";
/** Another class's pieces still go on, but the game draws none of them on this character. */
constexpr const char* kOtherClassWord = "%zu Other Class";
constexpr const char* kOtherClassTip = "Another class's gear equips, but won't show in the inventory screen.";
/** Footer: the name field and the save that uses it. */
constexpr const char* kNameHint = "Name for a New Loadout";
constexpr const char* kSaveLabel = "Save as New";
constexpr const char* kSaveTip = "Save what's equipped as a new loadout.";
constexpr float kSaveWidth = 120.0F;
/** 192 bytes hold any outcome the sheet reports, with a loadout name of the longest kind. */
constexpr std::size_t kMessageCapacity = 192;
/** 96 bytes hold one of a row's status words, or what it says in full. */
constexpr std::size_t kLineCapacity = 96;
/** Said when the file is there but could not be read, which is why nothing is saved over it. */
constexpr const char* kUnreadable =
    "Can't read Dawn/loadouts.json. Saving is off.";
/** Said in the head, and in place of the list, while the file can't be read. */
constexpr const char* kUnreadableWord = "Unreadable";
constexpr const char* kUnreadableEmpty = "Can't read your saved loadouts.";
constexpr const char* kUnreadableHint = "Fix or move Dawn/loadouts.json, then reopen.";
/** Said when the file could not be written. */
constexpr const char* kUnwritable = "Can't write Dawn/loadouts.json.";
/** Said in place of the list while this character has saved nothing. */
constexpr const char* kEmpty = "No saved loadouts yet.";
constexpr const char* kEmptyHint = "Name a loadout below, then choose Save as New.";

/** Where one saved loadout comes from, as the sheet for the character being edited sees it. */
enum class Origin : std::uint8_t {
    /** Another character's, which this sheet does not offer. */
    none,
    /** This character's own. */
    own,
    /**
     * Saved by a character no longer on the account, and for this one's class. It would otherwise
     * never be offered again, so it is offered here, and becomes this character's once it is used.
     */
    past,
};

/** What was pressed for one loadout this frame. */
enum class RowAction : std::uint8_t { none, choose, equip, saveOver, rename, remove, copy };

/** One press, and the character a copy goes to. */
struct Press {
    RowAction action{RowAction::none};
    std::size_t target{};
};

/** Where every piece of one loadout stands on this character, and what equipping it would do. */
struct Preview {
    /** Each piece as the equip would find it: where it stands, and the copy that would go on. */
    std::array<edit::PieceMatch, inv::kEquipmentSlotCount> matches{};
    std::size_t equipped{};
    std::size_t stowed{};
    std::size_t missing{};
    std::size_t unavailable{};
    /** Pieces of another class than this character's, which go on but do not show. */
    std::size_t otherClass{};
    /** Held pieces whose saved plugs differ from the ones fitted now. */
    std::size_t refits{};
    /** True when the saved abilities differ from the ones chosen now. */
    bool abilities{};

    /** @return True when equipping the loadout would change nothing. */
    [[nodiscard]] bool in_place() const noexcept {
        return equipped != 0 && stowed == 0 && missing == 0 && refits == 0 && !abilities;
    }
};

/**
 * Reads the saved loadouts the first time the sheet needs them, and again each time it opens while
 * the file can't be read, so one fixed or moved away is taken up on the next open. Nothing is ever
 * written while it can't be read, so a read then has nothing of the sheet's own to replace.
 */
void ensure_loaded() noexcept {
    Loadouts& loadouts = model().loadouts;
    if (loadouts.loaded && loadouts.writable) {
        return;
    }
    loadouts.loaded = true;
    loadouts.writable = presets::load(loadouts.entries);
}

/** Sets the outcome line for a sheet action that reaches no game, so nothing overwrites it. */
void report(const char* text, bool failed) noexcept {
    Model& state = model();
    state.status = text;
    state.statusFailed = failed;
}

/** Writes the saved loadouts. @return False, with the reason in the bar, when they could not be. */
[[nodiscard]] bool persist() noexcept {
    Model& state = model();
    if (!state.loadouts.writable) {
        report(kUnreadable, true);
        return false;
    }
    if (!presets::save(state.loadouts.entries)) {
        report(kUnwritable, true);
        return false;
    }
    return true;
}

/** @return The class a loadout is for: the first piece only one class can hold says so. */
[[nodiscard]] std::optional<std::uint8_t> loadout_class(const edit::SavedLoadout& entry) noexcept {
    for (const edit::SavedPiece& piece : entry.pieces) {
        const edit::CatalogItem* definition =
            piece.instance != 0 ? model().catalog.find(piece.definition) : nullptr;
        if (definition != nullptr && definition->characterClass < 3) {
            return definition->characterClass;
        }
    }
    return std::nullopt;
}

/** @return Where one saved loadout comes from, as this character's sheet sees it. */
[[nodiscard]] Origin origin_of(const edit::SavedLoadout& entry) noexcept {
    const state::CharacterState& owner = character();
    if (entry.character == owner.soid) {
        return Origin::own;
    }
    const state::AccountState& account = model().draft->after;
    for (std::size_t c = 0; c < account.characterCount; ++c) {
        if (account.characters[c].soid == entry.character) {
            return Origin::none;
        }
    }
    const std::optional<std::uint8_t> kind = loadout_class(entry);
    return !kind || *kind == static_cast<std::uint8_t>(owner.characterClass) ? Origin::past : Origin::none;
}

/** @return The other characters of this character's class, which a loadout can be copied to. */
[[nodiscard]] std::vector<std::size_t> copy_targets() noexcept {
    const Model& state = model();
    const state::AccountState& account = state.draft->after;
    std::vector<std::size_t> targets;
    for (std::size_t c = 0; c < account.characterCount; ++c) {
        if (c != state.character && account.characters[c].characterClass == character().characterClass) {
            targets.push_back(c);
        }
    }
    return targets;
}

/** @return The copy a piece was matched to, on whichever character holds it, or null when none is held. */
[[nodiscard]] const edit::Item* held_copy(const edit::PieceMatch& match) noexcept {
    const state::AccountState& account = model().draft->after;
    if (match.instance == 0 || match.holder >= account.characterCount) {
        return nullptr;
    }
    const state::CharacterState& holder = account.characters[match.holder];
    for (const auto& item : holder.equipment.slots) {
        if (item && item->instanceSoid == match.instance) {
            return &*item;
        }
    }
    for (std::size_t i = 0; i < holder.inventory.count; ++i) {
        if (holder.inventory.values[i].instanceSoid == match.instance) {
            return &holder.inventory.values[i];
        }
    }
    return nullptr;
}

/** @return Where every piece of one loadout stands, read the way the equip reads it. */
[[nodiscard]] Preview preview_of(const edit::SavedLoadout& entry) noexcept {
    const Model& state = model();
    Preview preview;
    for (std::size_t slot = 0; slot < preview.matches.size(); ++slot) {
        const edit::SavedPiece& piece = entry.pieces[slot];
        const edit::PieceMatch match = edit::match_piece(state.draft->after, state.character, state.catalog, piece, slot);
        preview.matches[slot] = match;
        preview.equipped += match.state == edit::PieceState::equipped ? 1U : 0U;
        preview.stowed += match.state == edit::PieceState::stowed ? 1U : 0U;
        preview.missing += match.state == edit::PieceState::missing ? 1U : 0U;
        preview.unavailable += match.state == edit::PieceState::unavailable ? 1U : 0U;
        const edit::CatalogItem* definition =
            match.state != edit::PieceState::empty ? state.catalog.find(piece.definition) : nullptr;
        preview.otherClass += definition != nullptr && !edit::fits_class(*definition, character().characterClass) ? 1U : 0U;
        const edit::Item* held = held_copy(match);
        preview.refits += held != nullptr && edit::refit_count(*held, state.catalog, piece) != 0 ? 1U : 0U;
    }
    preview.abilities = edit::abilities_differ(character(), state.catalog, entry);
    return preview;
}

/** @return How many loadouts the sheet offers this character. */
[[nodiscard]] std::size_t offered_count() noexcept {
    const Loadouts& loadouts = model().loadouts;
    return static_cast<std::size_t>(std::count_if(
        loadouts.entries.begin(), loadouts.entries.end(), [](const edit::SavedLoadout& entry) {
            return origin_of(entry) != Origin::none;
        }));
}

/** @return A typed name with the spaces around it taken off. */
[[nodiscard]] std::string trimmed(const char* text) noexcept {
    const std::string name = text;
    const std::size_t first = name.find_first_not_of(" \t");
    const std::size_t last = name.find_last_not_of(" \t");
    return first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
}

/**
 * Lets go of every question a row was asking and every name being typed for it. The list is about
 * to move under them, and they are held by index, so left standing they would pass to another row.
 */
void let_go() noexcept {
    Loadouts& loadouts = model().loadouts;
    loadouts.pendingDelete = -1;
    loadouts.pendingSave = -1;
    loadouts.renaming = -1;
}

/**
 * Chooses one row, and lets go of anything the row chosen before was in the middle of.
 * @param reveal True when the choice came from something other than the pointer, so the list
 * brings the row into view.
 */
void choose(int index, bool reveal = false) noexcept {
    Loadouts& loadouts = model().loadouts;
    if (loadouts.chosen != index) {
        let_go();
    }
    loadouts.chosen = index;
    loadouts.reveal = reveal;
}

/**
 * Writes a change the sheet made to the saved loadouts and keeps it for undo, or, when it cannot be
 * written, puts the list back as it was so what the sheet shows is what the file holds.
 * @param before The saved loadouts as they stood before the change.
 * @return True when the change was written.
 */
[[nodiscard]] bool commit(std::vector<edit::SavedLoadout> before) noexcept {
    Loadouts& loadouts = model().loadouts;
    if (!persist()) {
        loadouts.entries = std::move(before);
        return false;
    }
    record_loadouts_change(std::move(before));
    return true;
}

/** A loadout from an earlier character becomes this one's once it is used, so it is kept. */
void adopt(edit::SavedLoadout& entry) noexcept {
    entry.character = character().soid;
}

/** Saves what the character has equipped now, under the typed name or a numbered one. */
void save_current() noexcept {
    Loadouts& loadouts = model().loadouts;
    const state::CharacterState& owner = character();
    std::string name = trimmed(loadouts.name);
    if (name.empty()) {
        name = "Loadout " + std::to_string(offered_count() + 1);
    }
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    loadouts.entries.push_back(edit::capture_loadout(owner, model().catalog, name));
    if (!commit(std::move(before))) {
        return;
    }
    loadouts.name[0] = '\0';
    choose(static_cast<int>(loadouts.entries.size() - 1), true);
    char message[kMessageCapacity]{};
    (void)std::snprintf(message, sizeof message, "Saved %s.", name.c_str());
    report(message, false);
}

/** Replaces what one loadout holds with what is equipped now, keeping its name. */
void save_over(std::size_t index) noexcept {
    Loadouts& loadouts = model().loadouts;
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    edit::SavedLoadout& entry = loadouts.entries[index];
    entry = edit::capture_loadout(character(), model().catalog, entry.name);
    if (!commit(std::move(before))) {
        return;
    }
    char message[kMessageCapacity]{};
    (void)std::snprintf(message, sizeof message, "Saved over %s.", entry.name.c_str());
    report(message, false);
}

/** Starts renaming one loadout in place, with its name in the field and its row in view. */
void start_rename(std::size_t index) noexcept {
    Loadouts& loadouts = model().loadouts;
    choose(static_cast<int>(index), true);
    loadouts.renaming = static_cast<int>(index);
    loadouts.focusRename = true;
    (void)std::snprintf(loadouts.rename, sizeof loadouts.rename, "%s", loadouts.entries[index].name.c_str());
}

/** Gives one loadout the name typed for it. An empty name leaves it as it was. */
void finish_rename(std::size_t index) noexcept {
    Loadouts& loadouts = model().loadouts;
    loadouts.renaming = -1;
    edit::SavedLoadout& entry = loadouts.entries[index];
    const std::string name = trimmed(loadouts.rename);
    if (name.empty() || name == entry.name) {
        return;
    }
    const std::string previous = entry.name;
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    entry.name = name;
    adopt(entry);
    if (!commit(std::move(before))) {
        return;
    }
    char message[kMessageCapacity]{};
    (void)std::snprintf(message, sizeof message, "Renamed %s to %s.", previous.c_str(), name.c_str());
    report(message, false);
}

/** Deletes one loadout once its Delete has been confirmed, and chooses the row that takes its place. */
void delete_saved(std::size_t index) noexcept {
    Loadouts& loadouts = model().loadouts;
    let_go();
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    const std::string removed = loadouts.entries[index].name;
    loadouts.entries.erase(loadouts.entries.begin() + static_cast<std::ptrdiff_t>(index));
    if (!commit(std::move(before))) {
        return;
    }
    // The next row this sheet offers moves up into the place, or the last one when none follows.
    int next = -1;
    for (std::size_t i = 0; i < loadouts.entries.size(); ++i) {
        if (origin_of(loadouts.entries[i]) != Origin::none) {
            next = static_cast<int>(i);
            if (i >= index) {
                break;
            }
        }
    }
    choose(next);
    char message[kMessageCapacity]{};
    (void)std::snprintf(message, sizeof message, "Deleted %s.", removed.c_str());
    report(message, false);
}

/** Gives another character its own copy of one loadout, placed after the original. */
void copy_saved(std::size_t index, std::size_t target) noexcept {
    Loadouts& loadouts = model().loadouts;
    let_go();
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    edit::SavedLoadout copy = loadouts.entries[index];
    copy.character = model().draft->after.characters[target].soid;
    const std::string name = copy.name;
    loadouts.entries.insert(loadouts.entries.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
    if (!commit(std::move(before))) {
        return;
    }
    char message[kMessageCapacity]{};
    (void)std::snprintf(message,
                        sizeof message,
                        "Copied %s to %s.",
                        name.c_str(),
                        character_label(target).c_str());
    report(message, false);
}

/**
 * Moves one loadout to where another is, which is how a dragged row is dropped. The row being dragged
 * is named by index, which only stays true while the list holds still, so an index the list no longer
 * has is let go of rather than read.
 */
void move_saved(std::size_t from, std::size_t to) noexcept {
    Loadouts& loadouts = model().loadouts;
    if (from == to || from >= loadouts.entries.size() || to >= loadouts.entries.size()) {
        return;
    }
    let_go();
    std::vector<edit::SavedLoadout> before = loadouts.entries;
    edit::SavedLoadout moving = std::move(loadouts.entries[from]);
    loadouts.entries.erase(loadouts.entries.begin() + static_cast<std::ptrdiff_t>(from));
    loadouts.entries.insert(loadouts.entries.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
    if (!commit(std::move(before))) {
        return;
    }
    choose(static_cast<int>(to));
}

/**
 * Equips one saved loadout and says what it came to. A piece whose copy has gone is made again from
 * the build, and the loadout is pointed at the new copy so the next equip finds it rather than making
 * another. A piece the build no longer carries is counted in the message rather than refusing the rest.
 */
void equip_saved(edit::SavedLoadout& loadout) noexcept {
    Model& state = model();
    edit::LoadoutResult result;
    std::string refused;
    const bool changed = edit::apply_loadout(*state.draft,
                                             state.catalog,
                                             state.character,
                                             loadout,
                                             level_of(state.grant.power),
                                             result,
                                             refused);
    char message[kMessageCapacity]{};
    const bool nothingOn = result.equipped == 0 && result.recreated == 0;
    // Only an equip that changed nothing can stop here: one that refitted a piece it then could not
    // put on has still changed the draft, which has to be recorded and published like any other edit.
    if (nothingOn && !changed) {
        (void)std::snprintf(message,
                            sizeof message,
                            "None of %s's pieces are available on this character.",
                            loadout.name.c_str());
        report(message, true);
        return;
    }
    // Pointing the loadout at the copies it made or took up belongs to the equip, so the equip's step
    // takes it with it and one undo takes back both.
    bool repointed = true;
    const bool adopted = loadout.character != character().soid;
    const bool retargeted = std::any_of(result.replaced.begin(), result.replaced.end(), [](std::uint64_t id) { return id != 0; });
    if (retargeted || adopted) {
        std::vector<edit::SavedLoadout> before = state.loadouts.entries;
        adopt(loadout);
        for (std::size_t slot = 0; slot < result.replaced.size(); ++slot) {
            if (result.replaced[slot] != 0) {
                loadout.pieces[slot].instance = result.replaced[slot];
            }
        }
        repointed = persist();
        if (repointed) {
            note_loadouts_change(std::move(before));
        } else {
            state.loadouts.entries = std::move(before);
        }
    }
    int written = !changed ? std::snprintf(message, sizeof message, "%s", kInPlaceTip)
                           : std::snprintf(message,
                                           sizeof message,
                                           nothingOn ? "None of %s's pieces could be equipped." : "Equipped %s.",
                                           loadout.name.c_str());
    const auto append = [&](const char* format, auto... values) {
        if (written >= 0 && static_cast<std::size_t>(written) < sizeof message) {
            written += std::snprintf(message + written, sizeof message - static_cast<std::size_t>(written), format, values...);
        }
    };
    if (result.brought != 0) {
        append(" Brought %zu %s from other characters.", result.brought, result.brought == 1 ? "piece" : "pieces");
    }
    if (result.recreated != 0) {
        append(" Recreated %zu missing %s.", result.recreated, result.recreated == 1 ? "piece" : "pieces");
    }
    if (result.refitted != 0) {
        append(" Restored %zu %s as saved.", result.refitted, result.refitted == 1 ? "piece" : "pieces");
    }
    std::size_t otherClass = 0;
    for (const edit::SavedPiece& piece : loadout.pieces) {
        const edit::CatalogItem* definition = piece.instance != 0 ? state.catalog.find(piece.definition) : nullptr;
        otherClass += definition != nullptr && !edit::fits_class(*definition, character().characterClass) ? 1U : 0U;
    }
    if (otherClass != 0) {
        append(" %zu other-class %s won't show in the inventory screen.", otherClass, otherClass == 1 ? "piece" : "pieces");
    }
    // The noun counts every piece and the verb the unavailable ones: "1 of 8 pieces is unavailable."
    if (result.unavailable != 0) {
        append(" %zu of %zu %s %s unavailable.",
               result.unavailable,
               result.saved,
               result.saved == 1 ? "piece" : "pieces",
               result.unavailable == 1 ? "is" : "are");
    }
    if (!repointed) {
        append(" Can't save the pieces it used to Dawn/loadouts.json.");
    }
    if (!changed) {
        report(message, false);
        return;
    }
    // The apply that publishes this writes its own outcome over the bar, so what the loadout came
    // to is carried in front of it rather than lost under it.
    state.status = message;
    state.editNote = message;
    record_edit(true);
}

/**
 * Puts one saved piece back over an item as equipping would: the level it was saved at, when it was
 * saved with one, and each saved plug the build still carries. The sheet previews a piece through
 * this, so what it shows is what the equip would leave.
 */
void fit_saved(edit::Item& item, const edit::SavedPiece& piece) noexcept {
    const edit::Catalog& catalog = model().catalog;
    if (piece.level > 0) {
        item.level = (std::min)(piece.level, edit::kMaximumItemLevel);
    }
    if (!edit::materialize(item, catalog)) {
        return;
    }
    for (std::size_t lane = 0; lane < piece.plugs.size() && lane < item.sockets.plugCount; ++lane) {
        const edit::CatalogItem* plug = piece.plugs[lane] != 0 ? catalog.find(piece.plugs[lane]) : nullptr;
        if (plug != nullptr && plug->plug) {
            item.sockets.plugs[lane] = piece.plugs[lane];
        }
    }
}

/**
 * @return The copy equipping a loadout would make of one piece that has gone: the stock item put back
 * as it was saved. Gear saved before levels were kept is made at the level a new item is added at, as
 * the equip makes it, never at the level of what it replaces.
 */
[[nodiscard]] edit::Item remade_copy(const edit::SavedPiece& piece) noexcept {
    const Model& state = model();
    edit::Item copy;
    copy.definitionHash = piece.definition;
    // A non-zero id, so the tooltip titles the copy by its own level rather than the grant's.
    copy.instanceSoid = piece.instance;
    const edit::CatalogItem* definition = state.catalog.find(piece.definition);
    const bool powered = definition != nullptr
                         && (definition->kind == edit::GearKind::weapon || definition->kind == edit::GearKind::armor);
    copy.level = powered ? std::clamp(level_of(state.grant.power), 0, edit::kMaximumItemLevel) : 0;
    fit_saved(copy, piece);
    return copy;
}

/**
 * @return The item one saved piece reads as once the loadout is equipped: the copy it was matched to
 * with its saved level and plugs put back, or the copy that would be made of it, or nothing when it
 * is neither.
 */
[[nodiscard]] std::optional<edit::Item> equipped_as(const edit::SavedPiece& piece, const edit::PieceMatch& match) noexcept {
    if (match.state == edit::PieceState::missing) {
        return remade_copy(piece);
    }
    const edit::Item* held = held_copy(match);
    if (held == nullptr) {
        return std::nullopt;
    }
    edit::Item refitted = *held;
    fit_saved(refitted, piece);
    return refitted;
}

/**
 * Draws one saved piece: its icon framed in its rarity, veiled when the piece is unavailable, or a
 * recess where the slot was empty or the build no longer carries the item. Under the pointer it
 * shows the item's own tooltip, as a card does, as the piece will be once the loadout is equipped.
 */
void draw_piece(const edit::SavedPiece& piece, const edit::PieceMatch& match, ImVec2 at, float extent, bool rowHovered) noexcept {
    const Model& state = model();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner{at.x + extent, at.y + extent};
    const edit::CatalogItem* definition =
        match.state == edit::PieceState::empty ? nullptr : state.catalog.find(piece.definition);
    if (definition == nullptr) {
        draw->AddRectFilled(at, corner, ImGui::GetColorU32(ImGuiCol_FrameBg), pixels(controls::kRowRounding));
        return;
    }
    art::icon(*definition, at, extent);
    if (match.state == edit::PieceState::unavailable) {
        draw->AddRectFilled(at, corner, ImGui::GetColorU32(kUnavailableVeil));
    }
    // Nothing is described under a row being dragged, which is carrying the pointer elsewhere.
    if (!rowHovered || ImGui::GetDragDropPayload() != nullptr || !ImGui::IsMouseHoveringRect(at, corner)) {
        return;
    }
    const std::optional<edit::Item> item = equipped_as(piece, match);
    tooltip::draw(*definition, item ? &*item : nullptr);
}

/**
 * @return The armor stats one loadout adds up to on this character, as the character screen adds
 * them up: each piece's shown stats as the piece will be once equipped. A piece that is unavailable
 * adds nothing. `counted` says whether any armor did.
 */
[[nodiscard]] edit::Stats loadout_totals(const edit::SavedLoadout& entry, const Preview& preview, bool& counted) noexcept {
    const Model& state = model();
    edit::Stats totals{};
    counted = false;
    for (std::size_t slot = kLastWeaponSlot + 1; slot <= kLastArmorSlot; ++slot) {
        const std::optional<edit::Item> item = equipped_as(entry.pieces[slot], preview.matches[slot]);
        if (!item) {
            continue;
        }
        counted = true;
        const edit::Stats shown = edit::shown_stats(*item, state.catalog);
        for (std::size_t i = 0; i < totals.size(); ++i) {
            totals[i] += shown[i];
        }
    }
    return totals;
}

/** @return The width a word button takes for its label. */
[[nodiscard]] float word_width(const char* label) noexcept {
    return ImGui::CalcTextSize(label).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
}

/** @return The width every action before Delete shares: the widest word any of them shows, asking included. */
[[nodiscard]] float action_width() noexcept {
    float width = 0.0F;
    for (const char* label : {kEquipLabel, kEquippedLabel, kSaveOverLabel, kSaveConfirmLabel, kRenameLabel, kCopyLabel}) {
        width = (std::max)(width, word_width(label));
    }
    return width;
}

/**
 * @return The width Delete and Save as New share, so the two buttons at the far end line up one over
 * the other: the save's own width, unless a word of either needs more.
 */
[[nodiscard]] float end_width() noexcept {
    return (std::max)({word_width(kDeleteLabel), word_width(kDeleteConfirmLabel), word_width(kSaveLabel), pixels(kSaveWidth)});
}

/** Shows what the action just drawn does while the pointer is on it, including while it is disabled. */
void explain(const char* tip) noexcept {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tip);
    }
}

/**
 * Draws a word button that asks once more before it acts: pressed once it turns into its
 * confirmation, in the colour the action deserves, at a width that holds both words so nothing moves
 * under the pointer between the two presses.
 * @return True when it was pressed, whether to ask or to confirm.
 */
[[nodiscard]] bool confirming_button(const char* label,
                                     const char* confirm,
                                     bool asking,
                                     const ImVec4& color,
                                     const char* tip,
                                     const char* confirmTip,
                                     float width) noexcept {
    if (asking) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{color.x, color.y, color.z, kAskingFill});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{color.x, color.y, color.z, kAskingFillHovered});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{color.x, color.y, color.z, kAskingFillActive});
    }
    const bool pressed = ImGui::Button(asking ? confirm : label, {width, pixels(controls::kActionHeight)});
    if (asking) {
        ImGui::PopStyleColor(3);
    }
    explain(asking ? confirmTip : tip);
    return pressed;
}

/**
 * Draws the chosen loadout's actions on one line from the cursor: Equip first, as the one the sheet
 * is for, and Delete at the far end, away from it.
 * @param right Screen X the line ends at.
 */
[[nodiscard]] Press draw_actions(const Preview& preview,
                                 bool confirmingSave,
                                 bool confirmingDelete,
                                 const std::vector<std::size_t>& targets,
                                 float right) noexcept {
    const Loadouts& loadouts = model().loadouts;
    Press press;
    // The actions before Delete share one width, so the line reads as one row of equal buttons. When
    // Copy To joins them and the row would push Delete off the line, they narrow together instead.
    const float gap = pixels(kActionGap);
    const float endWidth = end_width();
    const float count = targets.empty() ? 3.0F : 4.0F;
    const float room = right - ImGui::GetCursorScreenPos().x - endWidth - (count * gap);
    const ImVec2 size{(std::max)(0.0F, (std::min)(action_width(), room / count)), pixels(controls::kActionHeight)};
    // The white button is the sheet's one action, so it is only white while it can act: a loadout
    // already on keeps the place with a plain button that says so.
    const bool inPlace = preview.in_place();
    if (inPlace) {
        ImGui::BeginDisabled();
        (void)ImGui::Button(kEquippedLabel, size);
        ImGui::EndDisabled();
    } else if (controls::primary_button(kEquipLabel, size)) {
        press.action = RowAction::equip;
    }
    explain(inPlace ? kInPlaceTip : kEquipTip);
    ImGui::BeginDisabled(!loadouts.writable);
    ImGui::SameLine(0.0F, gap);
    if (confirming_button(kSaveOverLabel, kSaveConfirmLabel, confirmingSave, tooltip::pending(), kSaveOverTip, kSaveConfirmTip, size.x)) {
        press.action = RowAction::saveOver;
    }
    ImGui::SameLine(0.0F, gap);
    if (ImGui::Button(kRenameLabel, size)) {
        press.action = RowAction::rename;
    }
    explain(kRenameTip);
    if (!targets.empty()) {
        ImGui::SameLine(0.0F, gap);
        if (ImGui::Button(kCopyLabel, size)) {
            ImGui::OpenPopup(kCopyMenu);
        }
        explain(kCopyTip);
        if (controls::begin_menu(kCopyMenu)) {
            for (const std::size_t target : targets) {
                if (ImGui::Selectable(character_label(target).c_str())) {
                    press = {RowAction::copy, target};
                }
            }
            controls::end_menu();
        }
    }
    ImGui::SameLine(0.0F, gap);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({(std::max)(at.x, right - endWidth), at.y});
    if (confirming_button(kDeleteLabel,
                          kDeleteConfirmLabel,
                          confirmingDelete,
                          controls::kRefusedColor,
                          kDeleteTip,
                          kDeleteConfirmTip,
                          endWidth)) {
        press.action = RowAction::remove;
    }
    ImGui::EndDisabled();
    return press;
}

/**
 * Sets a name within a width, in the cut already pushed, and cuts it short only when it runs past it.
 * @return True when it was cut, so the caller can show it whole under the pointer.
 */
[[nodiscard]] bool draw_name(const std::string& name, ImVec2 at, float width, float weight) noexcept {
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    if (ImGui::CalcTextSize(name.c_str()).x + weight <= width) {
        art::bold_text(name.c_str(), at, color, weight);
        return false;
    }
    art::clipped_text(name, at, width, color, weight);
    return true;
}

/**
 * Draws the name line's far end: the words saying where the loadout stands, in the spaced capitals
 * the tabs are set in and coloured by what they say, joined as a detail line is and set right to
 * left, the most telling first. The name keeps its share of the line whatever they say: a word that
 * would reach into it is cut, with every word after it, and under the pointer every word is said in
 * full.
 * @param kept Screen X the name's share of the line runs to, which the words and the gap before
 * them stay clear of.
 * @param pointed True while the pointer is on the row and no row is being dragged.
 * @return Where the name must stop: the gap short of where the words begin.
 */
[[nodiscard]] float draw_status(const Preview& preview,
                                Origin source,
                                float right,
                                float kept,
                                float top,
                                bool pointed) noexcept {
    // Each word, what it says in full, and its colour: the loadout on now in white, as the game
    // marks what is equipped, a piece that can't be put on in the pending colour, the rest muted.
    struct Word {
        std::string text;
        std::string meaning;
        ImU32 color{};
    };
    const ImU32 muted = ImGui::GetColorU32(tooltip::muted());
    std::vector<Word> words;
    char word[kLineCapacity]{};
    char said[kLineCapacity]{};
    if (preview.in_place()) {
        words.push_back({kInPlaceWord, kInPlaceTip, ImGui::GetColorU32(ImGuiCol_Text)});
    } else if (preview.unavailable != 0 || preview.missing != 0) {
        // The row says one thing about its pieces, the one that matters most: a piece the equip has
        // to leave behind, or else one it will make again, which is worth saying but is no fault.
        // Under the pointer both are said in full.
        std::string meaning;
        if (preview.unavailable != 0) {
            (void)std::snprintf(said,
                                sizeof said,
                                "%zu %s unavailable.",
                                preview.unavailable,
                                preview.unavailable == 1 ? "piece is" : "pieces are");
            meaning = said;
        }
        if (preview.missing != 0) {
            (void)std::snprintf(said,
                                sizeof said,
                                "Equipping recreates %zu missing %s.",
                                preview.missing,
                                preview.missing == 1 ? "piece" : "pieces");
            meaning += (meaning.empty() ? "" : "\n") + std::string(said);
        }
        const bool left = preview.unavailable != 0;
        (void)std::snprintf(word, sizeof word, left ? "%zu unavailable" : "%zu missing", left ? preview.unavailable : preview.missing);
        words.push_back({word, meaning, left ? ImGui::GetColorU32(controls::kPendingColor) : muted});
    }
    if (preview.otherClass != 0) {
        (void)std::snprintf(word, sizeof word, kOtherClassWord, preview.otherClass);
        words.push_back({word, kOtherClassTip, ImGui::GetColorU32(controls::kPendingColor)});
    }
    if (source == Origin::past) {
        words.push_back({kPastWord, kPastTip, muted});
    }
    const float gap = pixels(kWordGap);
    const float separator = controls::spaced_width(controls::kDetailSeparator);
    std::size_t shown = 0;
    float width = 0.0F;
    std::string full;
    bool cut = false;
    for (const Word& each : words) {
        full += (full.empty() ? "" : "\n") + each.meaning;
        const float grown = width + (shown == 0 ? 0.0F : separator) + controls::spaced_width(each.text.c_str());
        cut = cut || (right - grown - gap < kept);
        if (!cut) {
            width = grown;
            ++shown;
        }
    }
    if (shown == 0) {
        return right;
    }
    // Laid right to left, so the most telling word keeps the end of the line.
    float x = right;
    for (std::size_t i = 0; i < shown; ++i) {
        if (i != 0) {
            x -= separator;
            (void)controls::spaced(controls::kDetailSeparator, {x, top}, muted);
        }
        x -= controls::spaced_width(words[i].text.c_str());
        (void)controls::spaced(words[i].text.c_str(), {x, top}, words[i].color);
    }
    const float start = right - width;
    if (pointed && ImGui::IsMouseHoveringRect({start, top}, {right, top + ImGui::GetTextLineHeight()})) {
        ImGui::SetTooltip("%s", full.c_str());
    }
    return start - gap;
}

/**
 * Draws one loadout row and reports what was pressed on it: the row itself chooses the loadout and a
 * double-click equips it. A row can be dragged onto another to move it there.
 * @param index Loadout the row shows, as an index into the saved list.
 * @param source Where the loadout comes from.
 * @param width Row width in framebuffer pixels.
 * @param drop Receives the index of a row dropped onto this one.
 */
[[nodiscard]] Press draw_row(std::size_t index, Origin source, float width, int& drop) noexcept {
    Loadouts& loadouts = model().loadouts;
    const edit::SavedLoadout& entry = loadouts.entries[index];
    const ImGuiStyle& style = ImGui::GetStyle();
    auto* draw = ImGui::GetWindowDrawList();
    const float padding = pixels(kRowPadding);
    const float emblem = pixels(kEmblemExtent);
    const float piece = pixels(kPieceExtent);
    const bool chosen = loadouts.chosen == static_cast<int>(index);
    const bool renaming = loadouts.renaming == static_cast<int>(index);
    const Preview preview = preview_of(entry);

    const float rowHeight = emblem + (padding * 2.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner{origin.x + width, origin.y + rowHeight};
    if (chosen && loadouts.reveal) {
        ImGui::SetScrollHereY();
        loadouts.reveal = false;
    }

    // The hit target covers the whole row. It goes in first, so every mark after it is drawn over its fill.
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("loadout", {width, rowHeight});
    const bool hovered = ImGui::IsItemHovered();
    Press press;
    if (clicked) {
        press.action = RowAction::choose;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        press.action = RowAction::equip;
    }
    // A row is moved by dragging it onto another, while there is a file to write the order to. The
    // drag starts only past the pointer's threshold, so a click still chooses and a double-click
    // still equips.
    if (loadouts.writable && loadouts.renaming < 0 && ImGui::BeginDragDropSource()) {
        const int dragged = static_cast<int>(index);
        (void)ImGui::SetDragDropPayload(kRowPayload, &dragged, sizeof dragged);
        ImGui::TextUnformatted(art::shout(entry.name).c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            kRowPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload != nullptr && payload->DataSize == static_cast<int>(sizeof(int))) {
            const int dragged = *static_cast<const int*>(payload->Data);
            if (dragged != static_cast<int>(index)) {
                // The mark says where the row lands: under this one when it comes from above, over
                // it when it comes from below.
                const float y = dragged < static_cast<int>(index) ? corner.y : origin.y;
                draw->AddLine({origin.x, y}, {corner.x, y}, ImGui::GetColorU32(ImGuiCol_Text), pixels(controls::kRailWidth));
                if (payload->IsDelivery()) {
                    drop = dragged;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (chosen) {
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_Header), pixels(controls::kRowRounding));
        draw->AddRectFilled(origin, {origin.x + pixels(controls::kRailWidth), corner.y}, ImGui::GetColorU32(ImGuiCol_Text));
    } else if (hovered) {
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), pixels(controls::kRowRounding));
    }
    draw->AddLine({origin.x, corner.y}, {corner.x, corner.y}, ImGui::GetColorU32(tooltip::rule_color()));

    // The subclass stands as the loadout's emblem, at the height of the row, as the band's icon
    // stands at the height of the band.
    const float left = origin.x + padding;
    draw_piece(entry.pieces[kSubclassSlot], preview.matches[kSubclassSlot], {left, origin.y + padding}, emblem, hovered);

    // The name and the strip hang as one block, centred on the emblem.
    const float textLeft = left + emblem + pixels(kEmblemGap);
    const float right = corner.x - padding;
    const float nameSize = style.FontSizeBase * controls::kSubheadingScale;
    const float nameWeight = art::push_title(nameSize, 0.0F);
    const float nameLine = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    const float top = origin.y + ((rowHeight - (nameLine + pixels(kLineGap) + piece)) * 0.5F);
    const float baseline = top + (nameLine - ImGui::GetTextLineHeight());
    // Nothing is said under a row being dragged, which is carrying the pointer elsewhere.
    const bool pointed = hovered && ImGui::GetDragDropPayload() == nullptr;
    const float kept = textLeft + ((right - textLeft) * kNameShare);
    const float end = draw_status(preview, source, right, kept, baseline, pointed);
    const float nameWidth = (std::max)(0.0F, end - textLeft);
    if (renaming) {
        ImGui::SetCursorScreenPos({textLeft, top + ((nameLine - ImGui::GetFrameHeight()) * 0.5F)});
        ImGui::SetNextItemWidth(nameWidth);
        if (loadouts.focusRename) {
            ImGui::SetKeyboardFocusHere();
            loadouts.focusRename = false;
        }
        const bool entered = ImGui::InputText("##rename",
                                              loadouts.rename,
                                              sizeof loadouts.rename,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        // Enter or a click elsewhere keeps the name typed; Escape keeps the one it had.
        if (entered || (ImGui::IsItemDeactivated() && !ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            press.action = RowAction::rename;
        } else if (ImGui::IsItemDeactivated()) {
            loadouts.renaming = -1;
        }
    } else {
        (void)art::push_title(nameSize, 0.0F);
        const bool cut = draw_name(art::shout(entry.name), {textLeft, top}, nameWidth, nameWeight);
        ImGui::PopFont();
        if (cut && pointed && ImGui::IsMouseHoveringRect({textLeft, top}, {textLeft + nameWidth, top + nameLine})) {
            ImGui::SetTooltip("%s", entry.name.c_str());
        }
    }

    // The strip: the three weapons, a gap, then the five armor pieces.
    const float stripTop = top + nameLine + pixels(kLineGap);
    float x = textLeft;
    for (std::size_t slot = 0; slot <= kLastArmorSlot; ++slot) {
        if (slot == kLastWeaponSlot + 1) {
            x += pixels(kPieceGroupGap) - pixels(kPieceGap);
        }
        draw_piece(entry.pieces[slot], preview.matches[slot], {x, stripTop}, piece, hovered);
        x += piece + pixels(kPieceGap);
    }
    // The cursor is put back under the row, and an item submitted there so the list measures it.
    ImGui::SetCursorScreenPos({origin.x, corner.y});
    ImGui::Dummy({width, 0.0F});
    return press;
}

/** @return The height the strip under the list takes: its name and totals line, a gap, and its actions. */
[[nodiscard]] float strip_height() noexcept {
    return ImGui::GetTextLineHeight() + pixels(kDetailGap) + pixels(controls::kActionHeight);
}

/**
 * Draws the strip under the list, which holds the chosen loadout's name and armor totals on one
 * line and its actions under them. It stands in one place whichever row is chosen, so a second
 * press on a row can never land on an action that moved there.
 * @param chosen Loadout the strip is for, or -1 for none.
 * @param targets Characters a copy can go to.
 * @return What was pressed.
 */
[[nodiscard]] Press draw_strip(int chosen, bool empty, const std::vector<std::size_t>& targets) noexcept {
    const Loadouts& loadouts = model().loadouts;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    // With nothing saved, the list says so and says what to do; the strip adds no second instruction.
    // It keeps its height either way, so the list above it is measured the same whatever is chosen.
    if (chosen < 0) {
        if (!empty) {
            ImGui::GetWindowDrawList()->AddText(at, ImGui::GetColorU32(tooltip::muted()), kChooseHint);
        }
        ImGui::Dummy({width, strip_height()});
        return {};
    }
    const auto index = static_cast<std::size_t>(chosen);
    const edit::SavedLoadout& entry = loadouts.entries[index];
    const Preview preview = preview_of(entry);
    // The name is set as the row sets it, in the title cut, so the actions under it read as that
    // loadout's. It keeps a share of the line, cut short past it and shown whole under the pointer,
    // and the totals follow it a group's gap after wherever it ends.
    const std::string name = art::shout(entry.name);
    const float gap = pixels(controls::kGroupGap);
    const float room = (std::max)(0.0F, (width * kStripNameShare) - gap);
    const float weight = art::push_title(ImGui::GetStyle().FontSizeBase, 0.0F);
    const bool cut = draw_name(name, at, room, weight);
    const float nameWidth = (cut ? room : ImGui::CalcTextSize(name.c_str()).x) + gap;
    ImGui::PopFont();
    if (cut && ImGui::IsWindowHovered()
        && ImGui::IsMouseHoveringRect(at, {at.x + room, at.y + ImGui::GetTextLineHeight()})) {
        ImGui::SetTooltip("%s", entry.name.c_str());
    }
    bool counted = false;
    const edit::Stats totals = loadout_totals(entry, preview, counted);
    if (counted) {
        ImGui::SetCursorScreenPos({at.x + nameWidth, at.y});
        draw_stat_totals(totals, false);
    }
    ImGui::SetCursorScreenPos({at.x, at.y + ImGui::GetTextLineHeight() + pixels(kDetailGap)});
    return draw_actions(preview,
                        loadouts.pendingSave == chosen,
                        loadouts.pendingDelete == chosen,
                        targets,
                        at.x + width);
}

/** Carries out what was pressed for one loadout, once the list is no longer being drawn. */
void act(std::size_t index, const Press& press) noexcept {
    Loadouts& loadouts = model().loadouts;
    // Anything but a second press of the same action lets go of the question it was asking.
    if (press.action != RowAction::remove) {
        loadouts.pendingDelete = -1;
    }
    if (press.action != RowAction::saveOver) {
        loadouts.pendingSave = -1;
    }
    switch (press.action) {
    case RowAction::none:
        break;
    case RowAction::choose:
        choose(static_cast<int>(index));
        break;
    case RowAction::equip:
        choose(static_cast<int>(index));
        equip_saved(loadouts.entries[index]);
        break;
    case RowAction::saveOver:
        if (loadouts.pendingSave == static_cast<int>(index)) {
            loadouts.pendingSave = -1;
            save_over(index);
        } else {
            loadouts.pendingSave = static_cast<int>(index);
        }
        break;
    case RowAction::rename:
        if (loadouts.renaming == static_cast<int>(index)) {
            finish_rename(index);
        } else {
            start_rename(index);
        }
        break;
    case RowAction::remove:
        if (loadouts.pendingDelete == static_cast<int>(index)) {
            delete_saved(index);
        } else {
            loadouts.pendingDelete = static_cast<int>(index);
        }
        break;
    case RowAction::copy:
        copy_saved(index, press.target);
        break;
    }
}

/**
 * Takes the sheet's keys while no field is being typed in and no row is being dragged: the arrows
 * move between rows, Enter equips the chosen one, F2 renames it and Delete deletes it, asking once
 * more as the button does. While the row asks to confirm Save Over or Delete, Enter confirms it.
 * @param rows The loadouts the sheet shows, in order.
 * @return The row a key acted on and what it did, or no action.
 */
[[nodiscard]] std::pair<std::size_t, Press> take_keys(const std::vector<std::size_t>& rows) noexcept {
    Loadouts& loadouts = model().loadouts;
    // A dragged row is held by its index, which a key that changes the list would make name another.
    if (rows.empty() || ImGui::GetIO().WantTextInput || loadouts.renaming >= 0 || ImGui::GetDragDropPayload() != nullptr
        || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        return {0, {}};
    }
    const auto at = std::find(rows.begin(), rows.end(), static_cast<std::size_t>((std::max)(loadouts.chosen, 0)));
    const bool held = loadouts.chosen >= 0 && at != rows.end();
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        choose(static_cast<int>(!held ? rows.front() : at + 1 != rows.end() ? *(at + 1) : *at), true);
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        choose(static_cast<int>(!held ? rows.back() : at != rows.begin() ? *(at - 1) : *at), true);
    } else if (held && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        // A second press of the asking button is what confirms it, so Enter is that press.
        const int row = static_cast<int>(*at);
        const RowAction action = loadouts.pendingDelete == row ? RowAction::remove
                                 : loadouts.pendingSave == row ? RowAction::saveOver
                                                               : RowAction::equip;
        return {*at, {action, 0}};
    } else if (held && loadouts.writable && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        return {*at, {RowAction::rename, 0}};
    } else if (held && loadouts.writable && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        return {*at, {RowAction::remove, 0}};
    }
    return {0, {}};
}

/** @return The height of the footer: the outcome line, a row gap, and the row of actions. */
[[nodiscard]] float footer_height() noexcept {
    return ImGui::GetTextLineHeight() + pixels(controls::kRowSpacing) + pixels(controls::kActionHeight);
}

/**
 * Moves the cursor to the top of the footer, so it stands on the sheet's bottom edge whatever the
 * sheet holds above it. The foot of the content region is the sheet's height less its padding.
 * @return Where the row of actions starts, as a window position.
 */
[[nodiscard]] float anchor_footer() noexcept {
    const float foot = ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y;
    const float top = (std::max)(ImGui::GetCursorPosY(), foot - footer_height());
    ImGui::SetCursorPosY(top);
    return top + ImGui::GetTextLineHeight() + pixels(controls::kRowSpacing);
}

/**
 * Draws the footer on the sheet's bottom edge: what the last action came to on one line, then a
 * name and the save that uses it, at the height of its actions. The save ends the row under Delete.
 */
void draw_footer() noexcept {
    Model& state = model();
    Loadouts& loadouts = state.loadouts;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float save = end_width();
    const float action = pixels(controls::kActionHeight);
    const float actions = anchor_footer();
    draw_sheet_outcome(loadouts.writable ? nullptr : kUnreadable);
    ImGui::SetCursorPosY(actions);

    ImGui::BeginDisabled(!loadouts.writable);
    ImGui::SetNextItemWidth((std::max)(0.0F, right - ImGui::GetCursorPosX() - save - style.ItemSpacing.x));
    // The field stands as tall as the buttons beside it, so the footer is one row of one height.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2{style.FramePadding.x, (action - ImGui::GetTextLineHeight()) * 0.5F});
    // A text field reports Enter itself; it is only the number fields that cannot.
    const bool entered = ImGui::InputTextWithHint("##loadout_name",
                                                  kNameHint,
                                                  loadouts.name,
                                                  sizeof loadouts.name,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    ImGui::SameLine(right - save);
    // Equip is the one action the sheet is built around, so saving a new loadout is a plain button.
    if (ImGui::Button(kSaveLabel, {save, action}) || entered) {
        save_current();
    }
    explain(kSaveTip);
    ImGui::EndDisabled();
}

/**
 * Draws the two lines shown in place of the list while this character has nothing saved, or while
 * the file can't be read, when an empty list would say something untrue.
 */
void draw_empty() noexcept {
    const bool readable = model().loadouts.writable;
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const float left = ImGui::GetCursorPosX();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (std::max)(0.0F, (room.y - (line * 2.0F)) * 0.5F));
    for (const auto& [text, muted] : {std::pair{readable ? kEmpty : kUnreadableEmpty, false},
                                      std::pair{readable ? kEmptyHint : kUnreadableHint, true}}) {
        ImGui::SetCursorPosX(left + (std::max)(0.0F, (room.x - ImGui::CalcTextSize(text).x) * 0.5F));
        ImGui::TextColored(muted ? tooltip::muted() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", text);
    }
}

} // namespace

void open_loadouts() noexcept {
    ensure_loaded();
    model().statusAtSheet = model().status;
    Loadouts& loadouts = model().loadouts;
    loadouts.pendingDelete = -1;
    loadouts.pendingSave = -1;
    loadouts.renaming = -1;
    loadouts.chosen = -1;
    loadouts.chooseOnOpen = true;
    ImGui::OpenPopup(kLoadoutsTitle);
}

void draw_loadouts_modal() noexcept {
    // The sheet the perk picker is, so the editor's sheets are one frame, head, ground and size.
    if (!tooltip::begin_sheet(kLoadoutsTitle)) {
        return;
    }
    Model& state = model();
    Loadouts& loadouts = state.loadouts;

    // The rows this sheet offers, in the file's order, which is the order the player set by dragging.
    std::vector<std::size_t> rows;
    std::vector<Origin> sources;
    for (std::size_t i = 0; i < loadouts.entries.size(); ++i) {
        const Origin source = origin_of(loadouts.entries[i]);
        if (source != Origin::none) {
            rows.push_back(i);
            sources.push_back(source);
        }
    }
    // The sheet opens on the loadout that is on now, or else the first, so its actions are there at once.
    if (loadouts.chooseOnOpen) {
        loadouts.chooseOnOpen = false;
        const auto inPlace = std::find_if(rows.begin(), rows.end(), [&loadouts](std::size_t i) {
            return preview_of(loadouts.entries[i]).in_place();
        });
        choose(inPlace != rows.end() ? static_cast<int>(*inPlace)
               : rows.empty()        ? -1
                                     : static_cast<int>(rows.front()),
               true);
    }
    // A choice the list no longer offers is let go.
    if (loadouts.chosen >= 0
        && std::find(rows.begin(), rows.end(), static_cast<std::size_t>(loadouts.chosen)) == rows.end()) {
        choose(-1);
    }
    const std::vector<std::size_t> targets = copy_targets();

    // The head names the sheet and the character it is for, and while the file can't be read it says
    // so, as the list does.
    std::string line = character_label(state.character);
    if (!loadouts.writable) {
        line += std::string(controls::kDetailSeparator) + kUnreadableWord;
    }
    tooltip::draw_sheet_head(kLoadoutsTitle, line.c_str());
    controls::space(controls::kRuleSpacing);
    ImGui::Separator();

    std::size_t pressedRow = 0;
    Press pressed;
    int drop = -1;
    std::size_t dropOnto = 0;
    // Under the list: a rule and the gaps that hug it, which with no height of its own still takes
    // the item spacing after it; the strip, and the item spacing after its actions; a row gap; and
    // the footer, which stands on the sheet's bottom edge.
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float below = (pixels(controls::kRuleSpacing) * 2.0F) + spacing + strip_height() + spacing
                        + pixels(controls::kRowSpacing) + footer_height();
    const float listHeight = (std::max)(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y - below);
    // The rows set their own gaps, as the tooltip's rows do.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ImGui::GetStyle().ItemSpacing.x, 0.0F});
    if (ImGui::BeginChild("loadout_rows", {0.0F, listHeight})) {
        const float width = ImGui::GetContentRegionAvail().x;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            ImGui::PushID(static_cast<int>(rows[row]));
            int dropped = -1;
            const Press press = draw_row(rows[row], sources[row], width, dropped);
            ImGui::PopID();
            if (press.action != RowAction::none) {
                pressedRow = rows[row];
                pressed = press;
            }
            if (dropped >= 0) {
                drop = dropped;
                dropOnto = rows[row];
            }
        }
        if (rows.empty()) {
            draw_empty();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    controls::space(controls::kRuleSpacing);
    ImGui::Separator();
    controls::space(controls::kRuleSpacing);
    ImGui::PushID("strip");
    const Press stripPress = draw_strip(loadouts.chosen, rows.empty(), targets);
    ImGui::PopID();
    if (stripPress.action != RowAction::none && loadouts.chosen >= 0) {
        pressedRow = static_cast<std::size_t>(loadouts.chosen);
        pressed = stripPress;
    }

    // Keys act on the chosen row as its buttons do.
    if (pressed.action == RowAction::none) {
        const auto [keyedRow, keyed] = take_keys(rows);
        pressedRow = keyedRow;
        pressed = keyed;
    }
    // Escape first closes a menu the sheet has open, then lets go of a question waiting to be
    // answered, and only then closes the sheet. A rename it cancels was let go of by its own field,
    // which is still taking the keys this frame.
    bool close = false;
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false)
                        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (escape && (loadouts.pendingDelete >= 0 || loadouts.pendingSave >= 0)) {
        loadouts.pendingDelete = -1;
        loadouts.pendingSave = -1;
    } else if (escape && !ImGui::GetIO().WantTextInput && loadouts.renaming < 0) {
        close = true;
    }
    // The list is left alone while it is drawn; what was pressed on it is carried out after it.
    if (drop >= 0) {
        move_saved(static_cast<std::size_t>(drop), dropOnto);
    } else if (pressed.action != RowAction::none) {
        act(pressedRow, pressed);
    }

    draw_footer();
    if (close) {
        ImGui::CloseCurrentPopup();
    }
    tooltip::end_sheet();
}

} // namespace dawn::core::ui::modules::loadout::internal
