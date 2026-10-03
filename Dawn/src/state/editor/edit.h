#pragma once
#include "catalog.h"
#include "../equipment/light/definition.h"
#include <iterator>
#include <limits>
#include <random>
#include <tuple>

namespace dawn::state::editor {
using Item = account::inventory::Item;
using Stats = std::array<int, 6>;
/**
 * Highest item level the editor writes: the last whose Power, ten per level, still fits the game's
 * 32-bit Power. Dawn asks only for a level of zero or more, so this is the one bound that is real.
 */
inline constexpr int kMaximumItemLevel =
    (std::numeric_limits<std::int32_t>::max)() / equipment::light::kPowerPerLevel;
struct Draft {
    AccountState before, after;
    bool dirty{};
};
inline constexpr const char* kSlots[]{"Kinetic", "Energy", "Power", "Helmet", "Gauntlets", "Chest", "Legs", "Class Item", "Ghost", "Sparrow", "Ship", "Subclass", "Clan Banner", "Emblem", "Emote", "Finisher"};
// The enum lives in another header, so a slot added there would otherwise read past this table
// on every page that names a slot.
static_assert(std::size(kSlots) == account::inventory::kEquipmentSlotCount, "Every equipment slot needs a name.");
/**
 * @return The name the installed build gives the character stat at one index.
 * The icon and the value for a stat both come from `Catalog::statRows[index]`, so its label is
 * taken from the same row rather than from the list above, which only agrees with them while the
 * build's own row order happens to match it. The list stands in when the bank names nothing.
 * @param catalog Loaded catalog holding the stat rows and their names.
 * @param index Character stat index, matching `Stats` and `kStats`.
 */
[[nodiscard]] std::string_view stat_label(const Catalog& catalog, std::size_t index) noexcept;
static_assert(std::size(kStats) == std::tuple_size_v<Stats>, "Every character stat needs a name.");
bool materialize(Item& item, const Catalog& catalog);
/**
 * @return The damage type one weapon deals as it stands. A weapon that takes its damage type from
 * a plug deals the damage type of the plug fitted now, which can differ from the one it came with.
 * @param owned Instance whose fitted plugs decide, or null for the weapon as it comes.
 */
[[nodiscard]] DamageType item_damage_type(const CatalogItem& definition, const Item* owned, const Catalog& catalog) noexcept;
bool set_plug(Item& item, const Catalog& catalog, std::size_t lane, std::uint16_t plug, PlugScope scope);
Stats item_stats(const Item& item, const Catalog& catalog);
/**
 * @return One item's stats as its tooltip shows them: each stored value through the curve of the
 * item's own stat group. Summing these is what the character screen does for its armor totals.
 */
Stats shown_stats(const Item& item, const Catalog& catalog);
/** @return The stats one plug adds, as stored, in `Stats` order. */
Stats plug_stats(const CatalogItem& plug, const Catalog& catalog);
/**
 * @return The stat plugs one lane may take in place of the plug it holds, that plug included. An
 * allocation lane draws on every allocation plug of its group the installed armor rolls; any other
 * lane stays inside its own pool.
 * @param current The plug in the lane now, or null for an empty lane, which only an allocation lane
 *        can refill.
 */
std::vector<std::uint16_t> stat_plug_choices(const CatalogItem& definition, std::size_t lane, const CatalogItem* current, const Catalog& catalog);
/** @return True when one lane of an armor definition is one of the four sockets its stats roll in. */
bool allocation_lane(const CatalogItem& definition, std::size_t lane) noexcept;
// Finds the closest supported stat-plug allocation and reports the values actually reached.
bool adjust_stats(Item& item, const Catalog& catalog, const Stats& targets, Stats& achieved);
// True when at least one of the item's stat-bearing sockets offers a different stat plug, which is
// the only case in which `adjust_stats` can change anything.
bool adjustable_stats(const Item& item, const Catalog& catalog);
bool give(Draft& draft, const Catalog& catalog, std::size_t character, std::uint32_t hash, int quantity, int power, bool equip, std::string& error);
bool equip(Draft& draft, const Catalog& catalog, std::size_t character, std::uint64_t id, std::string& error);
bool unequip(Draft& draft, const Catalog& catalog, std::size_t character, std::size_t slot, std::string& error);
/**
 * Pulls one item out of the character's postmaster into its own bucket, as the game's postmaster
 * does. An item there cannot be equipped, so without this the only thing left to do with one is
 * delete it. An account item that overflowed there, such as a material, is left for the game.
 */
bool pull_from_postmaster(Draft& draft, const Catalog& catalog, std::size_t character, std::uint64_t id, std::string& error);
/**
 * Moves one stowed item from one character to another. It keeps its identity, its rolls and its
 * sockets, and takes a new revision from the character that receives it, as a transfer does in game.
 */
bool transfer(Draft& draft, const Catalog& catalog, std::size_t from, std::size_t to, std::uint64_t id, std::string& error);

/**
 * One piece of a saved loadout: the instance that was equipped, the item it was, and its roll, so a
 * copy that has gone can be made again as it was.
 */
struct SavedPiece {
    std::uint64_t instance{};
    /**
     * The item the instance was. An instance id only names one item within one account, and a
     * definition leaves the build with the package that added it, so both are checked before the
     * piece is put back on.
     */
    std::uint32_t definition{};
    /** Item level it was saved at, or zero for a loadout saved before levels were kept. */
    std::int32_t level{};
    /** Plug fitted in each socket lane when saved, by definition hash, with zero for an empty lane. */
    std::vector<std::uint32_t> plugs;
    friend bool operator==(const SavedPiece&, const SavedPiece&) = default;
};
/** One saved loadout: what one character had equipped, and the abilities of its subclass. */
struct SavedLoadout {
    /** Character the loadout was saved from, the only one it is offered to. */
    std::uint64_t character{};
    std::string name;
    /** Piece per equipment slot, with a zero instance where the slot was empty. */
    std::array<SavedPiece, account::inventory::kEquipmentSlotCount> pieces{};
    /** Jump, grenade, super, melee and class ability entries, in the catalog's lane order. */
    std::array<std::uint8_t, std::tuple_size_v<decltype(CatalogItem::abilities)>> abilities{};
    friend bool operator==(const SavedLoadout&, const SavedLoadout&) = default;
};
/** What equipping a saved loadout came to. */
struct LoadoutResult {
    /** Pieces the loadout names. */
    std::size_t saved{};
    /** Pieces now equipped, whether they were put on or were already in place. */
    std::size_t equipped{};
    /** Pieces the account no longer held in any copy, made again from the build. */
    std::size_t recreated{};
    /** Pieces stowed on another character, brought over to this one. */
    std::size_t brought{};
    /** Pieces held whose saved level or plugs were put back. */
    std::size_t refitted{};
    /** Pieces skipped: gone from the build, not this class's, at the postmaster, or without room. */
    std::size_t unavailable{};
    /**
     * The copy each slot's piece turned out to be where that is not the copy saved, one made again or
     * one that took its place, or zero, so the loadout can name it next time.
     */
    std::array<std::uint64_t, account::inventory::kEquipmentSlotCount> replaced{};
};
/** Where one saved piece stands on a character now. */
enum class PieceState : std::uint8_t {
    /** The slot was empty when the loadout was saved. */
    empty,
    /** On now, as the copy saved or another copy of the same item. */
    equipped,
    /** Held and stowed, so equipping the loadout puts it on. */
    stowed,
    /**
     * The account holds no copy of the item any longer, but the build still carries it and this
     * class can hold it, so equipping the loadout makes it again.
     */
    missing,
    /** Gone from the build, not this class's, or waiting at the postmaster. */
    unavailable,
};
/** Where one saved piece stands, and the copy of it equipping the loadout would put on. */
struct PieceMatch {
    PieceState state{PieceState::empty};
    /** The copy that would go on, or zero while none is held. */
    std::uint64_t instance{};
    /** The character holding that copy, which is another one only for a copy that is brought over. */
    std::size_t holder{};
};
/**
 * @return Where one saved piece stands for one character now. The copy saved is looked for first;
 * when it has gone, another copy of the same item on the character takes its place, the one in the
 * piece's slot before any stowed, and has the saved level and plugs put back over its own; then the copy saved,
 * stowed on another character, which is brought over. Only an item the account no longer holds is
 * made again. Equipping a loadout and the sheet that previews it both read a piece through this, so
 * the preview never promises a piece the equip would skip.
 * @param slot The equipment slot the piece fills.
 */
PieceMatch match_piece(const AccountState& account, std::size_t characterIndex, const Catalog& catalog, const SavedPiece& piece, std::size_t slot) noexcept;
/**
 * @return How much of one held item equipping its saved piece would put back: its level, when the
 * piece was saved with one and the item is at another now, and each lane whose saved plug the build
 * still carries and the item does not hold now. A lane saved empty, or whose plug has left the build,
 * keeps what it has, and is not counted.
 */
std::size_t refit_count(const Item& item, const Catalog& catalog, const SavedPiece& piece) noexcept;
/**
 * @return True when equipping a loadout would change one of the character's ability choices. Only a
 * loadout whose subclass is the one on now can, which is also the only case the equip puts them back.
 */
bool abilities_differ(const CharacterState& character, const Catalog& catalog, const SavedLoadout& loadout) noexcept;
/** @return What one character has equipped now, with each piece's level and plugs, as a loadout. */
SavedLoadout capture_loadout(const CharacterState& character, const Catalog& catalog, std::string name);
/**
 * Equips a saved loadout on one character. Each piece is found as `match_piece` finds it: a copy held
 * has its saved level and plugs put back over its own, and only a piece the account no longer holds
 * is made again from the build, at its saved level with its saved plugs; one the build no longer
 * carries is skipped rather than refusing the rest. The subclass's abilities are put back only when
 * the saved subclass is the one equipped, and only entries it offers, so the result is always one
 * the game accepts.
 * @param fallbackLevel Level a piece of gear is made at when it was saved before levels were kept.
 * @return True when anything changed. `error` says why nothing did otherwise.
 */
bool apply_loadout(Draft& draft, const Catalog& catalog, std::size_t character, const SavedLoadout& loadout,
                   int fallbackLevel, LoadoutResult& result, std::string& error);
bool randomize(Draft& draft, const Catalog& catalog, std::size_t character, const std::array<bool, account::inventory::kEquipmentSlotCount>& slots, int power, std::mt19937& random, std::string& error);
bool prepare_commit(const Draft& draft, const Catalog& catalog, AccountState& output, std::string& error);

/**
 * One edit, kept so it can be taken back or made again: every character it changed, and the account
 * stacks if it changed them, as they stood on either side of it. Only what the editor edits is kept:
 * a character's identity, abilities, equipment and inventory, and the account's stacks. The rest of
 * the account moves with the game, and taking an edit back never puts any of that back.
 */
struct EditStep {
    struct Character {
        std::uint64_t soid{};
        CharacterState before, after;
    };
    std::vector<Character> characters;
    /** True when the edit changed the account stacks, which the two lists then hold. */
    bool stacks{};
    std::vector<account::inventory::ProfileItem> stacksBefore, stacksAfter;
};
/**
 * Captures the edit between two images of the account.
 * @return False when the editor's own fields agree between them, which leaves nothing to take back.
 * Revisions are not compared: the game renumbers what an apply changes, and that is no edit.
 */
bool capture_step(const AccountState& before, const AccountState& after, EditStep& step);
/**
 * Takes one edit back, or makes it again, in the draft. Each character the edit changed must still
 * stand as the edit left it, or as it found it for a redo; one the game has moved on since is
 * refused, and nothing is changed. A piece put back takes a new revision, as any other edit gives it,
 * unless it is exactly the piece the game already holds.
 * @param forward False to take the edit back, true to make it again.
 * @return True when the draft now carries the step. The draft is dirty only if it differs from the
 * committed account, so taking back an edit that was never applied can leave it clean.
 */
bool retrace(Draft& draft, const EditStep& step, bool forward, std::string& error);

/**
 * Repairs a freshly loaded draft the installed build would refuse to publish.
 * An account saved by an earlier editor carries one row whose generation the character counter
 * still points at, and the family-four character object will not encode until the counter leads
 * it. That leaves the game retrying the account refresh and never publishing that character.
 * @param draft Draft to repair in place. Only the working image is touched.
 * @param message Receives an explanation when a repair was staged.
 * @return True when the draft was changed and now needs applying.
 */
bool normalize(Draft& draft, std::string& message);
/**
 * Commits the draft and publishes it to the running game, with no restart.
 * The first apply of the process copies the player database aside, so one restore point covers
 * the whole editing session however many applies follow it.
 * @param draft Edits to commit. A committed draft is rebased onto the new account and made clean.
 * @param message Receives the user-facing outcome, whether or not the apply succeeds.
 * @param live Receives true when a signed-in peer took the change, false when it only saved.
 * @return True when the account now carries the draft. Nothing is changed otherwise.
 */
bool apply(Draft& draft, const Catalog& catalog, std::string& message, bool& live);
/**
 * Re-arms the running game with the account already committed.
 * An apply made before the player is signed in reaches nobody, so the editor calls this until a
 * peer takes it and the change shows in game.
 * @return True when at least one connected peer will reload the account.
 */
bool republish() noexcept;
}
