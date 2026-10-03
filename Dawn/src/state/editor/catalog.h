#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../account/account_state.h"
#include "../build_data/items/item_catalog.h"
#include "../build_data/items/details/definition.h"

namespace dawn::state::editor {
enum class GearKind { other, weapon, armor, cosmetic, subclass };
/**
 * How widely the perk picker looks for plugs, narrowest first: the socket's own pool; that socket
 * type on items of the same subtype, such as Auto Rifle or Helmet; that socket type on any item; any
 * socket on items of the same subtype; any socket on items of the same type, such as every weapon or
 * every armor piece; and every plug. The two widest gear scopes leave cosmetics to cosmetic sockets.
 * They follow Sundial's Item Subtype and Item Type.
 */
enum class PlugScope { compatible, socketAndSubtype, socket, subtype, itemType, all };
struct AbilityChoice { std::uint8_t entry{}; std::string name; };
struct SubclassPath { std::string name; std::uint8_t super{}, melee{}; std::vector<std::string> perks; };
/** One authored point on a stat's display curve: a stored value and what the game shows for it. */
struct StatDisplayPoint {
    std::int32_t investment{};
    std::int32_t display{};
};

/** One stat a group displays, with the curve that converts its stored value. */
struct ScaledStat {
    std::uint16_t definitionIndex{};
    /** The game shows this stat as a plain number, with no bar. */
    bool numeric{};
    /** A linear stat shows any value the curve does not name, rather than clamping to it. */
    bool linear{};
    std::vector<StatDisplayPoint> curve;
};

/** An icon that belongs to no investment record, so it has no row to be named by. */
inline constexpr std::uint32_t kNoIconRow = 0xFFFFFFFFU;

/**
 * An item definition the catalog leaves out that still names an icon. It is kept only so the icon
 * browser can say what uses an icon, which is why it carries nothing but names.
 */
struct IconOwner {
    std::uint32_t tag{};
    std::uint32_t hash{};
    std::string name;
    std::string type;
};

/** One icon container entry, from whichever package declares it. */
struct IconRow {
    std::uint32_t tag{};
    /** Row in the investment icon table, or `kNoIconRow` when no investment record indexes it. */
    std::uint32_t row{kNoIconRow};
    /** Index into `Catalog::iconPackages`. */
    std::uint16_t package{};
};

/**
 * Character stat names, in the order the game lists them.
 * The build stores its stats in its own order, so this is the display order alone; a page
 * walks `Catalog::statOrder` to visit the rows behind these names.
 */
inline constexpr const char* kStats[]{"Mobility", "Resilience", "Recovery", "Discipline", "Intellect", "Strength"};

/** One installed stat group: the stats it scales and the ceiling it clamps them to. */
struct StatGroup {
    std::int32_t maximumValue{};
    std::vector<ScaledStat> scaled;
};

/** Damage types a weapon can deal, with none for Kinetic. The installed build carries Arc, Solar and Void. */
enum class DamageType : std::uint8_t { none, arc, solar, void_ };
/** The energy type an armor piece holds, as Bungie names it. The installed build carries Arc, Solar and Void. */
enum class EnergyType : std::uint8_t { none, arc, solar, void_ };
/** The ammunition a weapon draws, as the client classifies it. Non-weapons carry none. */
enum class Ammo : std::uint8_t { none, primary, special, heavy };

/** An item whose stat block names no primary stat, which most items do not. */
inline constexpr std::uint16_t kNoStatRow = 0xFFFF;

/** An item that names no stat group shows its stored values unchanged. */
inline constexpr std::uint16_t kNoStatGroup = 0xFFFFU;

struct CatalogItem {
    build_data::items::Definition definition{};
    build_data::items::details::Definition detail{};
    std::string name, type, description, search;
    std::size_t slot{account::inventory::kEquipmentSlotCount};
    std::uint8_t characterClass{3};
    GearKind kind{GearKind::other};
    bool plug{}, internal{};
    /** The bank names nothing for this definition, so its name was made up from its hash. */
    bool unnamed{};
    /**
     * A plug whose sandbox perks are all declaration-only: the native registry marks none of
     * them live, so fitting it changes nothing the player can feel. The picker leaves these out
     * of its narrower scopes.
     */
    bool inert{};
    std::uint32_t iconTag{};
    /** Stat group whose curves display this item's stored stat values. */
    std::uint16_t statGroupIndex{kNoStatGroup};
    /**
     * Damage type this weapon deals, decoded from the sandbox perks it carries, or, for an older
     * weapon that carries none and takes its damage type from a plug, from the plugs it comes with.
     */
    DamageType damageType{DamageType::none};
    /** The damage type is the plug's rather than the weapon's own, so a different plug fitted changes it. */
    bool damageTypeFromPlug{};
    Ammo ammo{Ammo::none};
    /**
     * Stat row the item's stat block names as its primary, or `kNoStatRow`.
     * This is what the game titles the big figure on a tooltip: Power on a weapon, Defense on
     * armor, Speed on a sparrow.
     */
    std::uint16_t primaryStatRow{kNoStatRow};
    std::array<std::vector<std::uint16_t>, account::inventory::kPlugCapacity> compatible;
    // Jump, grenade, super, melee, class ability; indices address the native subclass entry list.
    std::array<std::vector<AbilityChoice>, 5> abilities;
    std::vector<SubclassPath> paths;
};
struct Catalog {
    std::vector<CatalogItem> items;
    std::unordered_map<std::uint32_t, std::size_t> hashes;
    std::unordered_map<std::uint16_t, std::size_t> indices;
    /** Every plug any item offers in a socket of one type, by socket type. */
    std::unordered_map<std::uint32_t, std::vector<std::uint16_t>> socketPools;
    /** Every plug items of one subtype offer in a socket of one type, by subtype and socket type. */
    std::map<std::pair<std::string, std::uint32_t>, std::vector<std::uint16_t>> subtypeSocketPools;
    /** Every plug, cosmetics aside, that items of one subtype offer in any socket, by subtype. */
    std::unordered_map<std::string, std::vector<std::uint16_t>> subtypePools;
    /** Every plug, cosmetics aside, that items of one type offer in any socket, by type. */
    std::unordered_map<std::uint64_t, std::vector<std::uint16_t>> typePools;
    std::vector<std::uint16_t> plugs;
    std::array<std::uint8_t, 6> statRows{};
    /** Installed stat groups, indexed by `CatalogItem::statGroupIndex`. */
    std::vector<StatGroup> statGroups;
    /** Localized stat name per stat row, for every stat an item can carry. */
    std::unordered_map<std::uint16_t, std::string> statNames;
    /** Package icon container per character stat, in the same order as `statRows`. Zero when the
        installed build offers no icon for that stat, and the page then shows its name alone. */
    std::array<std::uint32_t, 6> statIconTags{};
    /** Every stat that names an icon, as its stat row and the icon container, for the icon browser. */
    std::vector<std::pair<std::uint16_t, std::uint32_t>> statIcons;
    /** Item definitions outside `items` that name an icon. Empty until `sweep_icons` fills it. */
    std::vector<IconOwner> iconOwners;
    /**
     * The icons the icon browser walks, filtered by the package that declares each.
     * At load this holds only the investment icons; `sweep_icons` replaces it with every icon
     * container the installed packages declare, and every bare image the interface packages keep,
     * which is the only way to find an icon no record points at. The sweep reads every package's
     * entry table, so it runs only when asked for.
     */
    std::vector<IconRow> icons;
    /** Package families that declare an icon, in the order `IconRow::package` indexes them. */
    std::vector<std::string> iconPackages;
    /** True once `sweep_icons` has replaced `icons` with the whole installed set. */
    bool iconsSwept{};
    /** Entry class of an icon container, which the sweep searches the packages for. */
    std::uint32_t iconClass{};
    /**
     * Entry class of an image record: one layer of an icon container, and the form the interface
     * packages keep their art in, bare, with no container around it.
     */
    std::uint32_t imageClass{};
    /** Investment icon table row by tag, so a swept icon can still name the row that indexes it. */
    std::unordered_map<std::uint32_t, std::uint32_t> investmentIconRows;
    /** The game's own ammunition marks, indexed by `Ammo`. Index zero is never drawn. */
    std::array<std::uint32_t, 4> ammoIconTags{};
    /**
     * Character stat indices in the order the game lists them.
     * `statRows` is in the build's own storage order, which is not the order a player is shown, so
     * the pages walk this instead. Every entry is a valid index into `statRows`.
     */
    std::array<std::size_t, 6> statOrder{0, 1, 2, 3, 4, 5};
    const CatalogItem* find(std::uint32_t hash) const noexcept;
    const CatalogItem* index(std::uint16_t id) const noexcept;
    std::vector<std::uint16_t> candidates(const CatalogItem& item, std::size_t lane, PlugScope scope) const;
    void finish();
};
/**
 * Converts one stored stat value into the number the game displays.
 * The installed build stores investment values and shows them through its group's curve, so a
 * weapon's stored range is not the range on its tooltip.
 * @param catalog Loaded catalog holding the stat groups.
 * @param groupIndex Group the item names, or `kNoStatGroup`.
 * @param statRow Stat definition row being displayed.
 * @param investment Stored value from the item definition.
 * @return The displayed value, or the stored value when no curve covers it.
 */
[[nodiscard]] std::int32_t display_stat(const Catalog& catalog,
                                        std::uint16_t groupIndex,
                                        std::uint16_t statRow,
                                        std::int32_t investment) noexcept;

/**
 * Converts one stored stat value through one curve, for work that holds its own copy of the curve
 * rather than the catalog, as the armor planner's worker does.
 * @param scaled Curve that scales the stat, or null when its group does not.
 * @param investment Stored value.
 * @return The displayed value, or the stored value when no curve covers it.
 */
[[nodiscard]] std::int32_t display_stat(const ScaledStat* scaled, std::int32_t investment) noexcept;

/** @return The curve for one stat row inside a group, or null when the group does not scale it. */
[[nodiscard]] const ScaledStat* scaled_stat(const Catalog& catalog, std::uint16_t groupIndex, std::uint16_t statRow) noexcept;

/**
 * @return True when the game shows this stat as a number rather than a bar, as its group says.
 * @param catalog Loaded catalog holding the stat groups.
 * @param groupIndex Group the item names, or `kNoStatGroup`.
 * @param statRow Stat definition row being displayed.
 */
[[nodiscard]] bool numeric_stat(const Catalog& catalog,
                                std::uint16_t groupIndex,
                                std::uint16_t statRow) noexcept;

/**
 * @return The damage type one item deals, or none when it carries no damage marker.
 * @param detail Item detail carrying the sandbox perk list.
 */
[[nodiscard]] DamageType damage_type_of(const build_data::items::details::Definition& detail) noexcept;

std::string searchable(std::string value);
bool matches(const CatalogItem& item, const std::string& query);
bool fits_class(const CatalogItem& item, CharacterClass characterClass) noexcept;
// Runs on the editor's worker. No game assets or online manifest are bundled.
bool load_catalog(Catalog& output, std::atomic_bool& cancel, std::atomic_uint& progress, std::string& error);
/**
 * Sweeps every installed package for icon containers, for the icon browser.
 * Runs on a worker; the caller moves the result into the catalog on its own thread.
 * @param catalog Loaded catalog, read for the icon class and the investment rows.
 * @param icons Receives every icon container found, sorted by package, row and tag.
 * @param packages Receives the package families in the order `IconRow::package` indexes them.
 * @param owners Receives every item definition outside the catalog that names an icon.
 * @return True when the sweep completed, even if it found nothing.
 */
bool sweep_icons(const Catalog& catalog, std::vector<IconRow>& icons, std::vector<std::string>& packages,
                 std::vector<IconOwner>& owners);
} // namespace dawn::state::editor
