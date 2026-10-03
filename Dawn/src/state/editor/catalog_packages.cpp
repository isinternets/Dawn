// SPDX-License-Identifier: GPL-3.0-only
// Investment name/icon layouts adapted from Sundial; see vendor/sundial/NOTICE.md.
#include <Windows.h>
#include "catalog.h"
#include "localized_strings.h"
#include "../../client/content/items/packages/internal.h"
#include "../../middleware/content/packages/tables/ability_pool_reader.h"
#include "../runtime/state_account_transaction_helpers.h"
#include "../../../vendor/sundial/class_items.h"
#include <algorithm>
#include <memory>
#include <unordered_set>

namespace dawn::state::editor {
namespace {
namespace packages = client::content::items::packages;
namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;
namespace abilities = middleware::content::packages::tables::abilities;


/** Running state of the icon sweep, which runs through a plain function pointer. */
struct IconSweep {
    std::vector<IconRow>* icons{};
    std::vector<std::string>* packages{};
    /** Investment icon table rows by tag, so a swept icon can still report the row that names it. */
    const std::unordered_map<std::uint32_t, std::uint32_t>* rows{};
    std::unordered_map<std::string, std::uint16_t> families;
    bool overflowed{};
    /** True on the pass for bare images, which leaves out every package that has containers. */
    bool bare{};
    /** Packages whose containers were swept. Their bare images are only those containers' layers. */
    std::unordered_set<std::string> containerPackages;
};

/** Offset of an icon container's primary layer, which is an image record. */
constexpr std::size_t kIconPrimaryLayer = 0x14;

/**
 * Icon container rows holding the ammunition marks: one round in white, two in green, three in
 * violet, which is how the game colours primary, special and heavy.
 * No investment record points at them, so nothing can resolve them by lookup; the rows were found
 * by browsing the icon table and are named here.
 */
constexpr std::uint32_t kPrimaryAmmoIconRow = 8380;
constexpr std::uint32_t kSpecialAmmoIconRow = 8379;
constexpr std::uint32_t kHeavyAmmoIconRow = 8381;

/** Icons an installed directory may declare. Far above the ~16k the investment package carries. */
constexpr std::size_t kIconSweepLimit = 400000;

/** Investment globals slots and row classes of the item strings, the string banks and the icons. */
constexpr std::size_t kItemStringMapSlot = 33;
constexpr std::size_t kLocalizedIndexSlot = 72;
constexpr std::size_t kIconTableSlot = 75;
constexpr std::uint32_t kItemStringRowClass = 0x80805CDFU;
constexpr std::uint32_t kIconRowClass = 0x80802957U;
/** An item's strings: its icon row, then its name and its type as localized references. */
constexpr std::size_t kItemIconOffset = 0x80;
constexpr std::size_t kItemNameOffset = 0x84;
constexpr std::size_t kItemTypeOffset = 0x90;
/** One icon table row, and the container tag inside it. */
constexpr std::size_t kIconRowSize = 0x18;
constexpr std::size_t kIconRowTagOffset = 0x10;
/**
 * A subclass's socket-entry list: 64-byte entries from 0x10, each with its display hash, its plug
 * source, its group and its kind, and at 56 the pool that grants it. Offsets are Parhelion's.
 */
constexpr std::size_t kSubclassEntryArray = 0x10;
constexpr std::size_t kSubclassEntrySize = 64;
constexpr std::size_t kSubclassEntryLimit = 36;
constexpr std::size_t kSubclassEntryPoolOffset = 56;
/** The entry that leads the middle attunement, which is its own super when it brings one. */
constexpr std::size_t kMiddleAttunementLead = 20;
/**
 * Attunement names. A subclass display record lists each attunement's plug source beside the lore
 * row the client names it by, and the lore display table holds that row's name. An authored path's
 * own name is a lore row Parhelion adds. Slots, classes and offsets are Parhelion's.
 */
constexpr std::size_t kDisplayPathArray = 0x30;
constexpr std::uint32_t kDisplayPathRowClass = 0x80805C45U;
constexpr std::size_t kDisplayPathRowSize = 8;
constexpr std::size_t kLoreDisplaySlot = 34;
constexpr std::uint32_t kLoreDisplayClass = 0x80805ABAU;
constexpr std::size_t kLoreDisplayRowSize = 40;
constexpr std::size_t kLoreNameOffset = 12;
/** The stock subclasses' list rows: three to a class, one spare row between two classes. */
constexpr std::array<std::size_t, 9> kStockSubclassRows{1, 2, 3, 5, 6, 7, 9, 10, 11};

/** @return The class a stock subclass list row belongs to, or -1 for a row past the stock ones. */
int stock_subclass_class(std::size_t row) noexcept {
    if (row >= 1 && row <= 3) return 1;
    if (row >= 5 && row <= 7) return 0;
    if (row >= 9 && row <= 11) return 2;
    return -1;
}

/** Orders icons by package, then row, then tag, which is the order the browser walks them in. */
void sort_icons(std::vector<IconRow>& icons) {
    std::sort(icons.begin(), icons.end(), [](const IconRow& a, const IconRow& b) {
        if (a.package != b.package) return a.package < b.package;
        if (a.row != b.row) return a.row < b.row;
        return a.tag < b.tag;
    });
}

/** Records one swept icon container or bare image against the package family that declares it. */
bool visit_icon(void* context, const reader::ClassEntry& entry) noexcept {
    auto& sweep = *static_cast<IconSweep*>(context);
    try {
        if (sweep.icons->size() >= kIconSweepLimit) {
            sweep.overflowed = true;
            return false;
        }
        // The family name is borrowed for the call only, and is ASCII, so it is narrowed here.
        std::string family;
        family.reserve(entry.packageFamily.size());
        for (const wchar_t character : entry.packageFamily)
            family.push_back(character < 0x80 ? static_cast<char>(character) : '?');
        if (sweep.bare && sweep.containerPackages.contains(family)) return true;
        const auto [it, added] = sweep.families.try_emplace(
            family, static_cast<std::uint16_t>(sweep.packages->size()));
        if (added) sweep.packages->push_back(family);
        const auto row = sweep.rows->find(entry.tag);
        sweep.icons->push_back(
            {entry.tag, row != sweep.rows->end() ? row->second : kNoIconRow, it->second});
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * Investment stat string map, which pairs each stat with its localized name and icon index.
 * These offsets are Sundial's, which resolves the same table for its armor stat row.
 */
constexpr std::size_t kStatStringMapSlot = 59;
/**
 * The investment root's stat definition table.
 * A stat definition row leads with its own hash and sits at the same index as the stat string row
 * that names it, so a hash resolves to a name through the two together.
 */
constexpr std::size_t kStatDefinitionSlot = 95;
/**
 * Stat definition hashes for the two stats the game titles gear by.
 * The item's own record does not carry its primary stat anywhere this reader can reach: the stat
 * block's 0x10 field only groups items by family, and no stat definition hash appears anywhere in
 * an item's string blob. These two are read back from the definition table so the names stay the
 * bank's own, and the kind decides which one an item is titled by.
 */
constexpr std::uint32_t kPowerStatHash = 0x735CF023U;
constexpr std::uint32_t kDefenseStatHash = 0xE854FA8EU;
constexpr std::uint32_t kStatDefinitionClass = 0x80807D09U;
constexpr std::size_t kStatDefinitionRowSize = 32;
constexpr std::size_t kItemPrimaryStatOffset = 0x10;
constexpr std::uint32_t kStatStringMapClass = 0x80805CC9U;
constexpr std::size_t kStatStringRowSize = 36;
constexpr std::size_t kStatIconIndexOffset = 20;
/** The localized stat name sits at the start of its string row. */
constexpr std::size_t kStatNameOffset = 4;
/** A stat with no icon stores this in place of an index. */
constexpr std::uint16_t kNoStatIcon = 0xFFFFU;
/**
 * Stat groups and their display curves.
 * The installed build stores investment values and shows them through a group's curve, so these
 * are what turn a stored range into the range on the game's tooltip. Offsets are Sundial's.
 */
constexpr std::size_t kStatGroupMapSlot = 60;
constexpr std::uint32_t kStatGroupClass = 0x80805D02U;
constexpr std::size_t kStatGroupRowSize = 0x38;
constexpr std::size_t kStatGroupScaledOffset = 0x10;
constexpr std::size_t kStatGroupMaximumOffset = 0x30;
constexpr std::uint32_t kScaledStatClass = 0x80805D06U;
constexpr std::size_t kScaledStatRowSize = 0x18;
constexpr std::size_t kScaledStatNumericOffset = 1;
constexpr std::size_t kScaledStatLinearOffset = 3;
constexpr std::size_t kScaledStatCurveOffset = 0x08;
constexpr std::uint32_t kStatCurveClass = 0x80807D1AU;
constexpr std::size_t kStatCurveRowSize = 0x08;
/**
 * The finished sandbox-perk catalog, which is what an item's sandbox perk indices index.
 * Each primary row carries the perk hash, then a self-relative pointer to its detail row, whose
 * description reference sits eight bytes in. Offsets and classes are Sundial's.
 */
constexpr std::size_t kSandboxPerkTableSlot = 71;
constexpr std::uint32_t kSandboxPerkRowClass = 0x80805C9DU;
constexpr std::size_t kSandboxPerkRowSize = 0x18;
constexpr std::size_t kSandboxPerkDetailPointerOffset = 0x08;
constexpr std::size_t kSandboxPerkDescriptionOffset = 0x08;
/** The installed catalog carries 2481 rows; a wild count is rejected. */
constexpr std::uint64_t kSandboxPerkLimit = 65536;
/**
 * The investment root's per-index sandbox-perk metadata, row-aligned with the catalog above.
 * Byte six of each row is the liveness the native registry checks. Sundial's slot and offset.
 */
constexpr std::size_t kSandboxPerkMetadataSlot = 106;
constexpr std::uint32_t kSandboxPerkMetadataRowClass = 0x80807AAEU;
constexpr std::size_t kSandboxPerkMetadataRowSize = 8;
constexpr std::size_t kSandboxPerkLiveOffset = 6;
/** An item names its stat group through a typed resource pointer in its strings blob. */
constexpr std::size_t kItemStatGroupPointerOffset = 0x70;
constexpr std::uint32_t kItemStatGroupResourceClass = 0x80805CF1U;
constexpr std::size_t kItemStatGroupIndexOffset = 0x14;
/** No installed group carries more rows than these, so a wild count is rejected. */
constexpr std::uint64_t kStatGroupLimit = 512;
constexpr std::uint64_t kCurvePointLimit = 256;

/**
 * Reads one item's stat group index out of its strings blob.
 * @param itemStrings Whole item strings definition.
 * @return The group index, or `kNoStatGroup` when the item names none.
 */
/**
 * @return The ammunition class stored in the item's string definition, or none.
 * The typed field sits at 0x140 behind a class marker at 0x13C; a definition without the marker
 * carries no classification. Zero is an inherited value a few stock items use, so it reads as none.
 */
Ammo read_ammo(std::span<const std::byte> itemStrings) noexcept {
    constexpr std::size_t kClassOffset = 0x13C, kTypeOffset = 0x140;
    constexpr std::uint32_t kAmmoClass = 0x80805D1AU;
    std::uint32_t marker{};
    std::uint16_t value{};
    if (!strings::read(itemStrings, kClassOffset, marker) || marker != kAmmoClass
        || !strings::read(itemStrings, kTypeOffset, value)) return Ammo::none;
    switch (value) {
    case 1: return Ammo::primary;
    case 2: return Ammo::special;
    case 3: return Ammo::heavy;
    default: return Ammo::none;
    }
}

/** @return Offset of the item's stat block resource, or zero when it names none. */
std::size_t stat_block_resource(std::span<const std::byte> itemStrings) noexcept {
    std::int64_t relative{};
    if (!strings::read(itemStrings, kItemStatGroupPointerOffset, relative) || relative == 0) return 0;
    const auto resource = static_cast<std::int64_t>(kItemStatGroupPointerOffset) + relative;
    if (resource < 4 || static_cast<std::size_t>(resource) >= itemStrings.size()) return 0;
    std::uint32_t resourceClass{};
    if (!strings::read(itemStrings, static_cast<std::size_t>(resource) - 4, resourceClass)
        || resourceClass != kItemStatGroupResourceClass) return 0;
    return static_cast<std::size_t>(resource);
}

std::uint16_t read_stat_group_index(std::span<const std::byte> itemStrings) noexcept {
    std::int64_t relative{};
    if (!strings::read(itemStrings, kItemStatGroupPointerOffset, relative) || relative == 0) return kNoStatGroup;
    const auto resource = static_cast<std::int64_t>(kItemStatGroupPointerOffset) + relative;
    if (resource < 4 || static_cast<std::size_t>(resource) >= itemStrings.size()) return kNoStatGroup;
    std::uint32_t resourceClass{};
    if (!strings::read(itemStrings, static_cast<std::size_t>(resource) - 4, resourceClass)
        || resourceClass != kItemStatGroupResourceClass) return kNoStatGroup;
    std::int32_t index{};
    if (!strings::read(itemStrings, static_cast<std::size_t>(resource) + kItemStatGroupIndexOffset, index)
        || index < 0 || index >= static_cast<std::int32_t>(kNoStatGroup)) return kNoStatGroup;
    return static_cast<std::uint16_t>(index);
}

/**
 * Reads every installed stat group and the display curves it carries.
 * @param table Whole stat group table bytes.
 * @param rows Array descriptor for the group rows.
 * @param output Receives one entry per group, in table order.
 * @return True when every group and curve decodes.
 */
bool read_stat_groups(std::span<const std::byte> table, const tables::Array& rows,
                      std::vector<StatGroup>& output) {
    if (rows.elementClass != kStatGroupClass || rows.count > kStatGroupLimit) return false;
    output.assign(static_cast<std::size_t>(rows.count), StatGroup{});
    for (std::uint64_t group = 0; group < rows.count; ++group) {
        const std::size_t row = rows.dataOffset + static_cast<std::size_t>(group) * kStatGroupRowSize;
        StatGroup& target = output[static_cast<std::size_t>(group)];
        if (!strings::read(table, row + kStatGroupMaximumOffset, target.maximumValue)) return false;
        tables::Array scaledRows{};
        if (!tables::find_array_at(table, row + kStatGroupScaledOffset, scaledRows)) continue;
        if (scaledRows.count == 0) continue;
        if (scaledRows.elementClass != kScaledStatClass || scaledRows.count > kStatGroupLimit) return false;
        for (std::uint64_t scaled = 0; scaled < scaledRows.count; ++scaled) {
            const std::size_t at = scaledRows.dataOffset + static_cast<std::size_t>(scaled) * kScaledStatRowSize;
            std::uint8_t definitionIndex{};
            std::uint8_t numeric{};
            std::uint8_t linear{};
            if (!strings::read(table, at, definitionIndex)
                || !strings::read(table, at + kScaledStatNumericOffset, numeric)
                || !strings::read(table, at + kScaledStatLinearOffset, linear)) return false;
            ScaledStat entry;
            entry.definitionIndex = definitionIndex;
            entry.numeric = numeric == 1;
            entry.linear = linear == 1;
            tables::Array curveRows{};
            if (tables::find_array_at(table, at + kScaledStatCurveOffset, curveRows) && curveRows.count != 0) {
                if (curveRows.elementClass != kStatCurveClass || curveRows.count > kCurvePointLimit) return false;
                entry.curve.reserve(static_cast<std::size_t>(curveRows.count));
                for (std::uint64_t point = 0; point < curveRows.count; ++point) {
                    const std::size_t p = curveRows.dataOffset + static_cast<std::size_t>(point) * kStatCurveRowSize;
                    StatDisplayPoint value{};
                    // The first value is the stored input and the second is what the game shows.
                    if (!strings::read(table, p, value.investment)
                        || !strings::read(table, p + 4, value.display)) return false;
                    entry.curve.push_back(value);
                }
            }
            target.scaled.push_back(std::move(entry));
        }
    }
    return true;
}
struct ReadScope {
    reader::BlockKeys keys{};
    std::unique_ptr<reader::Scratch> scratch{std::make_unique<reader::Scratch>()};
    ~ReadScope() { reader::close_files(*scratch); SecureZeroMemory(&keys, sizeof keys); }
};
/**
 * Resolves localized string references through the investment's bank index, opening each bank
 * table once and keeping it. The catalog load and the icon sweep both name things through it.
 */
struct Bank {
    const reader::Source& source;
    reader::Scratch& scratch;
    const std::vector<std::byte>& index;
    const tables::Array& rows;
    std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::string>> banks;

    std::string operator()(std::span<const std::byte> data, std::size_t at) {
        std::uint32_t table{}, hash{};
        if (!strings::read(data, at, table) || !strings::read(data, at + 4, hash) || table >= rows.count) return {};
        auto it = banks.find(table);
        if (it == banks.end()) {
            std::unordered_map<std::uint32_t, std::string> values;
            std::uint32_t headerTag{}, dataTag{};
            std::vector<std::byte> header, bytes;
            if (strings::read(std::span<const std::byte>(index), rows.dataOffset + table * 8 + 4, headerTag)
                && reader::read_tag(source, scratch, headerTag, header)
                && strings::read(std::span<const std::byte>(header), 24, dataTag)
                && reader::read_tag(source, scratch, dataTag, bytes)) (void)strings::decode(header, bytes, values);
            it = banks.emplace(table, std::move(values)).first;
        }
        const auto found = it->second.find(hash);
        return found == it->second.end() ? std::string{} : found->second;
    }
};
/**
 * @return True when one socket entry's active pool variant declares a bucket kind of its own. A
 * middle attunement brings its own super only then; otherwise it adds to the shared super, which
 * stays the one selected. The data says which for every subclass, stock or authored. The pool is
 * walked by the ability reader the subclass bucket build already relies on.
 * @param entry Index of the entry in its list.
 * @param pool Scratch buffer the entry's pool is read into.
 */
bool declares_kind(const reader::Source& source, reader::Scratch& scratch, std::span<const std::byte> list,
                   std::size_t entry, std::vector<std::byte>& pool) {
    std::array<abilities::Entry, abilities::kEntryCapacity> entries{};
    std::array<abilities::PoolRecord, 1> first{};
    return entry < abilities::read_entries(list, entries)
        && reader::read_tag(source, scratch, entries[entry].poolTag, pool)
        && abilities::read_pool_records(pool, entries[entry], 0, first) != 0
        && first[0].kind != abilities::kEmptyByte;
}
/** @return The icon container one icon table row names, or zero. */
std::uint32_t icon_tag_of(std::span<const std::byte> table, const tables::Array& rows, std::size_t row) {
    std::uint32_t tag{};
    if (row < rows.count) (void)strings::read(table, rows.dataOffset + row * kIconRowSize + kIconRowTagOffset, tag);
    return tag;
}
/**
 * Names every item definition outside the catalog that carries an icon, so the icon browser can say
 * an icon belongs to an item the editor does not list. An item's strings are what name its icon.
 */
void collect_icon_owners(const Catalog& catalog, const reader::Source& source, reader::Scratch& scratch,
                         std::vector<IconOwner>& owners) {
    std::array<std::uint32_t, packages::kContainerCandidates> globalsTags{};
    std::size_t globalsCount{};
    if (!packages::investment_globals_tags(globalsTags, globalsCount)) return;
    std::vector<std::byte> globals, stringMap, localizedIndex, iconTable, itemStrings;
    tables::Array stringRows{}, localizedRows{}, iconRows{};
    bool located = false;
    for (std::size_t i = 0; i < globalsCount && !located; ++i) {
        std::uint32_t stringTag{}, localizedTag{}, iconTag{};
        located = reader::read_tag(source, scratch, globalsTags[i], globals)
            && tables::child_tag(globals, kItemStringMapSlot, stringTag)
            && tables::child_tag(globals, kLocalizedIndexSlot, localizedTag)
            && tables::child_tag(globals, kIconTableSlot, iconTag)
            && reader::read_tag(source, scratch, stringTag, stringMap)
            && reader::read_tag(source, scratch, localizedTag, localizedIndex)
            && reader::read_tag(source, scratch, iconTag, iconTable)
            && tables::find_array_at(stringMap, 8, stringRows) && stringRows.elementClass == kItemStringRowClass
            && tables::find_array_at(localizedIndex, 8, localizedRows)
            && tables::find_array_at(iconTable, 8, iconRows) && iconRows.elementClass == kIconRowClass;
    }
    if (!located) return;
    Bank resolve{source, scratch, localizedIndex, localizedRows, {}};
    for (std::size_t i = 0; i < stringRows.count; ++i) {
        tables::IndexRow row{};
        std::uint16_t icon{};
        if (!tables::index_row(stringMap, stringRows, i, row) || catalog.hashes.contains(row.definitionHash)
            || !reader::read_tag(source, scratch, row.targetTag, itemStrings)
            || !strings::read(std::span<const std::byte>(itemStrings), kItemIconOffset, icon)) continue;
        const std::uint32_t tag = icon_tag_of(iconTable, iconRows, icon);
        if (tag == 0) continue;
        owners.push_back({tag, row.definitionHash, resolve(itemStrings, kItemNameOffset),
                          resolve(itemStrings, kItemTypeOffset)});
    }
}
bool pool_member(void* context, std::uint16_t id) noexcept {
    static_cast<std::vector<std::uint16_t>*>(context)->push_back(id);
    return true;
}
}
bool load_catalog(Catalog& output, std::atomic_bool& cancel, std::atomic_uint& progress, std::string& error) {
    if (!build_data::configured_item_details_ready() || !build_data::socket_plug_rules_ready()) {
        error = "The installed item catalog is still loading. Try again after character selection appears.";
        return false;
    }
    ReadScope scope;
    core::path::Buffer directory{};
    if (!packages::package_directory(directory) || !packages::collect_keys(scope.keys)) {
        error = "Game packages are not ready to read yet."; return false;
    }
    reader::Source source{directory.chars.data(), &scope.keys};
    std::array<std::uint32_t, packages::kContainerCandidates> globalsTags{};
    std::size_t globalsCount{};
    std::vector<std::byte> globals, stringMap, localizedIndex, iconTable, statStrings, statGroupTable;
    tables::Array stringRows{}, localizedRows{}, iconRows{}, statStringRows{}, statGroupRows{};
    bool located = false;
    if (packages::investment_globals_tags(globalsTags, globalsCount)) {
        for (std::size_t i = 0; i < globalsCount && !located; ++i) {
            std::uint32_t stringTag{}, localizedTag{}, iconTag{}, statStringTag{}, statGroupTag{};
            located = reader::read_tag(source, *scope.scratch, globalsTags[i], globals)
                && tables::child_tag(globals, kItemStringMapSlot, stringTag)
                && tables::child_tag(globals, kLocalizedIndexSlot, localizedTag)
                && tables::child_tag(globals, kIconTableSlot, iconTag)
                && tables::child_tag(globals, kStatStringMapSlot, statStringTag)
                && reader::read_tag(source, *scope.scratch, statStringTag, statStrings)
                && tables::find_array_at(statStrings, 8, statStringRows)
                && statStringRows.elementClass == kStatStringMapClass
                && tables::child_tag(globals, kStatGroupMapSlot, statGroupTag)
                && reader::read_tag(source, *scope.scratch, statGroupTag, statGroupTable)
                && tables::find_array_at(statGroupTable, 8, statGroupRows)
                && reader::read_tag(source, *scope.scratch, stringTag, stringMap)
                && reader::read_tag(source, *scope.scratch, localizedTag, localizedIndex)
                && reader::read_tag(source, *scope.scratch, iconTag, iconTable)
                && tables::find_array_at(stringMap, 8, stringRows) && stringRows.elementClass == kItemStringRowClass
                && tables::find_array_at(localizedIndex, 8, localizedRows)
                && tables::find_array_at(iconTable, 8, iconRows) && iconRows.elementClass == kIconRowClass;
        }
    }
    if (!located) { error = "Could not read the installed item names and preview index."; return false; }
    std::unordered_map<std::uint32_t, std::uint32_t> tags;
    for (std::size_t i = 0; i < stringRows.count; ++i) {
        tables::IndexRow row{};
        if (tables::index_row(stringMap, stringRows, i, row)) tags[row.definitionHash] = row.targetTag;
    }
    std::unordered_map<std::uint32_t, bool> previewReferences;
    Bank resolve{source, *scope.scratch, localizedIndex, localizedRows, {}};
    Catalog result;
    build_data::constants::InvestmentConstants constants{};
    if (!build_data::find_investment_constants(constants)) { error = "Armor stat definitions are not ready."; return false; }
    result.statRows = constants.characterStatRows;
    // Stat names come from the same rows as the icons, but need the localized resolver above.
    for (std::uint64_t row = 0; row < statStringRows.count; ++row) {
        const std::size_t at = statStringRows.dataOffset + static_cast<std::size_t>(row) * kStatStringRowSize;
        std::string name = resolve(statStrings, at + kStatNameOffset);
        if (!name.empty()) result.statNames.emplace(static_cast<std::uint16_t>(row), std::move(name));
    }
    // Stored stat values are shown through their group's curve, so the groups are decoded once
    // and every displayed number goes through them.
    if (!read_stat_groups(statGroupTable, statGroupRows, result.statGroups)) result.statGroups.clear();
    // Each character stat names an icon in the same container table the items use, so the totals
    // row can show the game's own stat icons instead of names alone.
    for (std::size_t i = 0; i < result.statRows.size(); ++i) {
        const std::size_t row = result.statRows[i];
        std::uint16_t icon{};
        if (row >= statStringRows.count) continue;
        if (!strings::read(std::span<const std::byte>(statStrings),
                           statStringRows.dataOffset + row * kStatStringRowSize + kStatIconIndexOffset, icon)
            || icon == kNoStatIcon || icon >= iconRows.count) continue;
        (void)strings::read(std::span<const std::byte>(iconTable),
                            iconRows.dataOffset + icon * 0x18 + 0x10, result.statIconTags[i]);
    }
    // Every stat that names an icon, not only the six character stats, so the icon browser can say
    // which stat an icon marks.
    for (std::uint64_t row = 0; row < statStringRows.count; ++row) {
        std::uint16_t icon{};
        if (!strings::read(std::span<const std::byte>(statStrings),
                           statStringRows.dataOffset + static_cast<std::size_t>(row) * kStatStringRowSize
                               + kStatIconIndexOffset,
                           icon)
            || icon == kNoStatIcon) continue;
        const std::uint32_t tag = icon_tag_of(iconTable, iconRows, icon);
        if (tag != 0) result.statIcons.emplace_back(static_cast<std::uint16_t>(row), tag);
    }
    // Each stat definition leads with the hash an item names its primary stat by, and sits at the
    // same index as the stat string row that names it.
    std::unordered_map<std::uint32_t, std::uint16_t> primaryStatRows;
    {
        std::vector<std::byte> investmentRoot, statDefinitions;
        std::uint32_t rootTag{}, definitionTag{};
        tables::Array definitionRows{};
        if (tables::child_tag(globals, 0, rootTag)
            && reader::read_tag(source, *scope.scratch, rootTag, investmentRoot)
            && tables::slot_tag(investmentRoot, kStatDefinitionSlot, definitionTag)
            && reader::read_tag(source, *scope.scratch, definitionTag, statDefinitions)
            && tables::find_array_at(statDefinitions, 8, definitionRows)
            && definitionRows.elementClass == kStatDefinitionClass) {
            for (std::uint64_t row = 0; row < definitionRows.count; ++row) {
                std::uint32_t hash{};
                if (strings::read(std::span<const std::byte>(statDefinitions),
                                  definitionRows.dataOffset
                                      + static_cast<std::size_t>(row) * kStatDefinitionRowSize,
                                  hash)
                    && hash != 0)
                    primaryStatRows.emplace(hash, static_cast<std::uint16_t>(row));
            }
        }
    }

    // Nothing names an icon, so the icon browser is left to find one by eye. The investment table
    // is what loads here; the sweep of every other package that `sweep_icons` runs is deferred
    // until the browser asks, since it reads every package's entry table.
    {
        const auto icon_tag_at = [&](std::uint32_t row) {
            std::uint32_t tag{};
            if (row < iconRows.count)
                (void)strings::read(std::span<const std::byte>(iconTable),
                                    iconRows.dataOffset + row * 0x18 + 0x10, tag);
            return tag;
        };
        result.ammoIconTags[static_cast<std::size_t>(Ammo::primary)] = icon_tag_at(kPrimaryAmmoIconRow);
        result.ammoIconTags[static_cast<std::size_t>(Ammo::special)] = icon_tag_at(kSpecialAmmoIconRow);
        result.ammoIconTags[static_cast<std::size_t>(Ammo::heavy)] = icon_tag_at(kHeavyAmmoIconRow);

        std::unordered_map<std::uint32_t, std::uint32_t> investment;
        std::uint32_t sample = 0;
        for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(iconRows.count); ++row) {
            std::uint32_t tag{};
            if (!strings::read(std::span<const std::byte>(iconTable),
                               iconRows.dataOffset + row * 0x18 + 0x10, tag) || tag == 0) continue;
            investment.emplace(tag, row);
            if (sample == 0) sample = tag;
        }
        // The entry table records every tag's class, so one investment icon names the class the
        // rest of the installed packages would be swept for.
        std::vector<std::byte> probe;
        if (sample != 0) (void)reader::read_tag(source, *scope.scratch, sample, probe, result.iconClass);
        // Its primary layer names the class of an image record, the form interface art is kept in.
        std::uint32_t primary{};
        std::vector<std::byte> image;
        if (strings::read(std::span<const std::byte>(probe), kIconPrimaryLayer, primary) && primary != 0)
            (void)reader::read_tag(source, *scope.scratch, primary, image, result.imageClass);
        result.iconPackages.emplace_back("investment");
        for (const auto& [tag, row] : investment) result.icons.push_back({tag, row, 0});
        sort_icons(result.icons);
        result.investmentIconRows = std::move(investment);
    }
    // The build stores its character stats in its own order. The game lists them in the order
    // `kStats` names, so each of those names is matched to the row that carries it; a name the
    // bank does not resolve keeps its storage position rather than displacing one that did.
    {
        std::array<bool, 6> taken{};
        std::array<bool, 6> placed{};
        for (std::size_t wanted = 0; wanted < result.statOrder.size(); ++wanted) {
            for (std::size_t row = 0; row < result.statRows.size(); ++row) {
                if (taken[row]) continue;
                const auto name = result.statNames.find(result.statRows[row]);
                if (name == result.statNames.end() || name->second != kStats[wanted]) continue;
                result.statOrder[wanted] = row;
                taken[row] = placed[wanted] = true;
                break;
            }
        }
        // The bank leaves one character stat unnamed, so it never matches by name. It keeps its own
        // place in the display order and takes whichever row is left rather than falling to the end.
        std::size_t spare = 0;
        for (std::size_t wanted = 0; wanted < result.statOrder.size(); ++wanted) {
            if (placed[wanted]) continue;
            while (spare < taken.size() && taken[spare]) ++spare;
            if (spare >= taken.size()) break;
            result.statOrder[wanted] = spare;
            taken[spare] = true;
        }
    }
    const auto count = build_data::item_definition_count();
    std::vector<std::byte> itemStrings, iconDefinition;
    for (std::size_t i = 0; i < count && !cancel.load(); ++i) {
        CatalogItem item;
        if (!build_data::find_item_definition_index(static_cast<std::uint16_t>(i), item.definition)
            || !build_data::find_configured_item_detail(item.definition.definitionIndex, item.detail)) continue;
        item.characterClass = classes::find(item.definition.definitionHash);
        // Non-plug records can carry small scalar values at the legacy category fallback offset.
        item.plug = (item.definition.plugCategoryHash > 0xFFFFU && item.definition.plugCategoryHash != 0xFFFFFFFFU)
            || build_data::is_socket_plug_pooled(item.definition.definitionIndex);
        if (item.detail.equipmentSlot && *item.detail.equipmentSlot >= 0) {
            (void)runtime::detail::semantic_equipment_slot(static_cast<std::uint8_t>(*item.detail.equipmentSlot), item.slot);
            item.kind = item.slot <= 2 ? GearKind::weapon : item.slot <= 7 ? GearKind::armor
                : item.slot == 11 ? GearKind::subclass : item.slot < account::inventory::kEquipmentSlotCount ? GearKind::cosmetic : GearKind::other;
        }
        item.damageType = damage_type_of(item.detail);
        if (item.slot < account::inventory::kEquipmentSlotCount
            && item.detail.instancedDefinitionState == build_data::items::details::InstancedDefinitionState::instanced) item.plug = false;
        if (auto it = tags.find(item.definition.definitionHash); it != tags.end()
            && reader::read_tag(source, *scope.scratch, it->second, itemStrings)) {
            // Gear the stock lists do not hold, such as anything Parhelion authors, is named by the
            // class key its strings carry, as Sundial reads it.
            if (item.characterClass == 3) item.characterClass = classes::from_item_strings(itemStrings);
            item.name = resolve(itemStrings, 0x84);
            item.type = resolve(itemStrings, 0x90);
            item.description = resolve(itemStrings, 0x98);
            item.statGroupIndex = read_stat_group_index(std::span<const std::byte>(itemStrings));
            item.ammo = read_ammo(std::span<const std::byte>(itemStrings));
            const auto titledBy = item.kind == GearKind::weapon  ? kPowerStatHash
                                  : item.kind == GearKind::armor ? kDefenseStatHash
                                                                 : 0U;
            if (titledBy != 0) {
                const auto named = primaryStatRows.find(titledBy);
                if (named != primaryStatRows.end()) item.primaryStatRow = named->second;
            }
            std::uint16_t icon{};
            if (strings::read(std::span<const std::byte>(itemStrings), 0x80, icon) && icon < iconRows.count)
                (void)strings::read(std::span<const std::byte>(iconTable), iconRows.dataOffset + icon * 0x18 + 0x10, item.iconTag);
        }
        // An ornament may reuse a weapon/armor bucket; it belongs in cosmetics and the perk picker.
        const auto type = searchable(item.type);
        if (type.find("ornament") != std::string::npos || type.find("shader") != std::string::npos
            || type.find("transmat") != std::string::npos || type.find("projection") != std::string::npos) {
            item.kind = GearKind::cosmetic; item.slot = account::inventory::kEquipmentSlotCount; item.plug = true;
        }
        if (item.kind == GearKind::weapon || item.kind == GearKind::armor) {
            auto [preview, added] = previewReferences.try_emplace(item.iconTag, false);
            if (added) {
                std::uint32_t primary{};
                preview->second = reader::read_tag(source, *scope.scratch, item.iconTag, iconDefinition)
                    && strings::read(std::span<const std::byte>(iconDefinition), 0x14, primary)
                    && tables::package_of(primary) != tables::kAbsentPackageId;
            }
            // Placeholder/test definitions can name an icon container with only a background.
            if (!preview->second) { item.internal = true; item.iconTag = 0; }
        }
        for (std::size_t lane = 0; lane < item.detail.ordinarySocketCount; ++lane) {
            auto& pool = item.compatible[lane];
            (void)build_data::visit_socket_plug_pool(item.definition.definitionIndex, static_cast<std::uint8_t>(lane), &pool_member, &pool);
            (void)build_data::visit_socket_roll_pool(item.definition.definitionIndex, static_cast<std::uint8_t>(lane), &pool_member, &pool);
            if (item.detail.initialPlugIndices[lane] != build_data::items::details::kUnavailableItemIndex)
                pool.push_back(item.detail.initialPlugIndices[lane]);
        }
        result.items.push_back(std::move(item));
        progress.store(static_cast<unsigned>((i + 1) * 100 / count));
    }
    if (cancel.load()) { error = "Catalog loading cancelled."; return false; }
    // A mod's own strings often carry no description: what it does is written on the sandbox
    // perks it calls, in the finished sandbox-perk catalog. Each primary row points at a detail
    // row whose description reference sits eight bytes in. The layout is Sundial's.
    {
        std::vector<std::byte> perkTable;
        std::uint32_t perkTag{};
        tables::Array perkRows{};
        std::vector<std::string> perkDescriptions;
        if (tables::child_tag(globals, kSandboxPerkTableSlot, perkTag)
            && reader::read_tag(source, *scope.scratch, perkTag, perkTable)
            && tables::find_array_at(perkTable, 8, perkRows)
            && perkRows.elementClass == kSandboxPerkRowClass
            && perkRows.count <= kSandboxPerkLimit) {
            perkDescriptions.resize(static_cast<std::size_t>(perkRows.count));
            for (std::uint64_t row = 0; row < perkRows.count; ++row) {
                const std::size_t at = perkRows.dataOffset + static_cast<std::size_t>(row) * kSandboxPerkRowSize;
                std::int64_t pointer{};
                std::size_t detail{};
                if (!strings::read(std::span<const std::byte>(perkTable), at + kSandboxPerkDetailPointerOffset, pointer)
                    || pointer == 0
                    || !strings::relative(std::span<const std::byte>(perkTable), at + kSandboxPerkDetailPointerOffset, detail)) continue;
                perkDescriptions[static_cast<std::size_t>(row)] = resolve(perkTable, detail + kSandboxPerkDescriptionOffset);
            }
        }
        // The root's per-index perk metadata says which perks the native registry treats as
        // live; a plug whose perks are all declaration-only is marked inert.
        std::vector<bool> live;
        {
            std::vector<std::byte> rootBlob, metadata;
            std::uint32_t rootTag{}, metadataTag{};
            tables::Array rows{};
            if (tables::child_tag(globals, 0, rootTag)
                && reader::read_tag(source, *scope.scratch, rootTag, rootBlob)
                && tables::slot_tag(rootBlob, kSandboxPerkMetadataSlot, metadataTag)
                && reader::read_tag(source, *scope.scratch, metadataTag, metadata)
                && tables::find_array_at(metadata, 8, rows)
                && rows.elementClass == kSandboxPerkMetadataRowClass
                && rows.count <= kSandboxPerkLimit) {
                live.resize(static_cast<std::size_t>(rows.count));
                for (std::uint64_t row = 0; row < rows.count; ++row) {
                    std::uint8_t flag{};
                    const std::size_t at = rows.dataOffset + static_cast<std::size_t>(row) * kSandboxPerkMetadataRowSize;
                    if (strings::read(std::span<const std::byte>(metadata), at + kSandboxPerkLiveOffset, flag))
                        live[static_cast<std::size_t>(row)] = flag != 0;
                }
            }
        }
        for (auto& item : result.items) {
            if (!item.plug) continue;
            const std::size_t declared = (std::min)(static_cast<std::size_t>(item.detail.sandboxPerkCount), item.detail.sandboxPerks.size());
            if (declared > 0 && !live.empty()) {
                bool anyLive = false;
                for (std::size_t i = 0; i < declared; ++i) {
                    const std::size_t index = item.detail.sandboxPerks[i];
                    anyLive = anyLive || (index < live.size() && live[index]);
                }
                item.inert = !anyLive;
            }
            if (!item.description.empty()) continue;
            const std::size_t perks = (std::min)(static_cast<std::size_t>(item.detail.sandboxPerkCount), item.detail.sandboxPerks.size());
            for (std::size_t i = 0; i < perks; ++i) {
                const std::size_t index = item.detail.sandboxPerks[i];
                if (index >= perkDescriptions.size()) continue;
                const std::string& text = perkDescriptions[index];
                if (text.empty() || item.description.find(text) != std::string::npos) continue;
                if (!item.description.empty()) item.description += "\n\n";
                item.description += text;
            }
        }
    }
    // Sundial's parallel subclass displays provide localized ability and path names.
    std::vector<std::byte> root, listIndex, displays, list, displayRecord, abilityDisplay, poolRecord;
    std::uint32_t tag{}; tables::Array listRows{}, displayRows{};
    if (tables::child_tag(globals, 0, tag) && reader::read_tag(source, *scope.scratch, tag, root)
        && tables::slot_tag(root, 97, tag) && reader::read_tag(source, *scope.scratch, tag, listIndex)
        && tables::find_array_at(listIndex, 8, listRows)
        && tables::child_tag(globals, 61, tag) && reader::read_tag(source, *scope.scratch, tag, displays)
        && tables::find_array_at(displays, 8, displayRows)) {
        const auto read_list = [&](std::size_t id, tables::Array& entries) {
            tables::IndexRow row{};
            return tables::index_row(listIndex, listRows, id, row)
                && reader::read_tag(source, *scope.scratch, row.targetTag, list)
                && tables::find_array_at(list, kSubclassEntryArray, entries) && entries.count <= kSubclassEntryLimit
                && entries.count <= (list.size() - entries.dataOffset) / kSubclassEntrySize;
        };
        const auto class_base_pool = [&](const tables::Array& entries, std::uint32_t& pool) {
            return entries.count != 0
                && strings::read(std::span<const std::byte>(list), entries.dataOffset + kSubclassEntryPoolOffset, pool);
        };
        // Parhelion gives a subclass it authors a list row of its own, past the stock rows. The list
        // keeps its base's first entry, the class-base melee, whose pool differs by class, so the
        // stock rows say which pool is which class.
        std::unordered_map<std::uint32_t, std::uint8_t> classPools;
        for (const std::size_t id : kStockSubclassRows) {
            tables::Array entries{};
            std::uint32_t pool{};
            if (read_list(id, entries) && class_base_pool(entries, pool))
                classPools.emplace(pool, static_cast<std::uint8_t>(stock_subclass_class(id)));
        }
        std::vector<std::byte> loreStrings;
        tables::Array loreRows{};
        std::uint32_t loreTag{};
        const bool loreReady = tables::child_tag(globals, kLoreDisplaySlot, loreTag)
            && reader::read_tag(source, *scope.scratch, loreTag, loreStrings)
            && tables::find_array_at(loreStrings, 8, loreRows) && loreRows.elementClass == kLoreDisplayClass;
        // The name of the attunement one plug source leads, from the lore row the subclass's display
        // record shows it by. Empty when the record names no row for it.
        const auto lore_name = [&](std::uint32_t plugSource) -> std::string {
            tables::Array pathRows{};
            if (!loreReady || !tables::find_array_at(displayRecord, kDisplayPathArray, pathRows)
                || pathRows.elementClass != kDisplayPathRowClass) return {};
            for (std::uint64_t r = 0; r < pathRows.count; ++r) {
                const std::size_t at = pathRows.dataOffset + static_cast<std::size_t>(r) * kDisplayPathRowSize;
                std::uint32_t pathSource{}, loreRow{};
                if (!strings::read(std::span<const std::byte>(displayRecord), at, pathSource) || pathSource != plugSource
                    || !strings::read(std::span<const std::byte>(displayRecord), at + 4, loreRow)
                    || loreRow >= loreRows.count) continue;
                return resolve(loreStrings,
                               loreRows.dataOffset + static_cast<std::size_t>(loreRow) * kLoreDisplayRowSize + kLoreNameOffset);
            }
            return {};
        };
        for (auto& item : result.items) if (item.kind == GearKind::subclass) {
            const auto id = item.detail.socketEntryListIndex;
            const int stock = stock_subclass_class(id);
            if (stock >= 0) item.characterClass = static_cast<std::uint8_t>(stock);
            // A stock subclass is named by its own item strings, as all gear is, from its class's bank.
            tables::IndexRow row{}; tables::Array entries{};
            if (!read_list(id, entries)) continue;
            if (stock < 0) {
                std::uint32_t pool{};
                const auto known = class_base_pool(entries, pool) ? classPools.find(pool) : classPools.end();
                if (known == classPools.end()) continue;
                item.characterClass = known->second;
            }
            if (!tables::index_row(displays, displayRows, id, row) || !reader::read_tag(source, *scope.scratch, row.targetTag, displayRecord)) continue;
            std::unordered_map<std::uint32_t, std::string> names;
            std::vector<std::uint32_t> localTables;
            for (std::size_t at = 16; at + 4 <= displayRecord.size(); at += 4) {
                std::uint32_t displayTag{}, displayHash{}, cls{}, localTable{};
                if (!strings::read(std::span<const std::byte>(displayRecord), at, displayTag)
                    || tables::package_of(displayTag) == tables::kAbsentPackageId
                    || !reader::read_tag(source, *scope.scratch, displayTag, abilityDisplay, cls) || cls != 0x80805C49U
                    || !strings::read(std::span<const std::byte>(displayRecord), at - 16, displayHash)) continue;
                names[displayHash] = resolve(abilityDisplay, 160);
                if (strings::read(std::span<const std::byte>(abilityDisplay), 160, localTable)) localTables.push_back(localTable);
            }
            struct Entry { AbilityChoice choice; std::uint32_t source{}; std::uint8_t group{}; };
            std::vector<Entry> options;
            for (std::size_t i = 0; i < entries.count; ++i) {
                const auto base = entries.dataOffset + i * kSubclassEntrySize; std::uint32_t displayHash{};
                Entry entry; entry.choice.entry = static_cast<std::uint8_t>(i);
                (void)strings::read(std::span<const std::byte>(list), base, displayHash);
                (void)strings::read(std::span<const std::byte>(list), base + 8, entry.source);
                (void)strings::read(std::span<const std::byte>(list), base + 12, entry.group);
                entry.choice.name = names[displayHash];
                if (entry.choice.name.empty()) entry.choice.name = "Ability " + std::to_string(i);
                options.push_back(std::move(entry));
            }
            for (auto i : {4U,5U,6U}) if (i < options.size()) item.abilities[0].push_back(options[i].choice);
            for (auto i : {7U,8U,9U}) if (i < options.size()) item.abilities[1].push_back(options[i].choice);
            for (auto i : {2U,3U}) if (i < options.size()) item.abilities[4].push_back(options[i].choice);
            std::vector<std::uint32_t> sources;
            for (const auto& entry : options) if (entry.group == 3 && entry.source != account::inventory::kNoDefinitionHash
                && std::find(sources.begin(), sources.end(), entry.source) == sources.end()) sources.push_back(entry.source);
            constexpr std::uint32_t pathHashes[]{0xDF417340U,0x730873A5U,0x761AF51AU};
            constexpr const char* pathFallback[]{"Top path","Bottom path","Middle path"};
            const bool middleSuper = declares_kind(source, *scope.scratch, list, kMiddleAttunementLead, poolRecord);
            for (std::size_t p = 0; p < sources.size() && p < 3; ++p) {
                // The lore row names every path, stock or authored. The stock names the ability banks
                // carry, and last the path's place, stand in only for a record that names no row.
                SubclassPath path; path.name = lore_name(sources[p]);
                if (path.name.empty())
                    for (auto local : localTables) if (auto bank = resolve.banks.find(local); bank != resolve.banks.end())
                        if (auto name = bank->second.find(pathHashes[p]); name != bank->second.end()) { path.name = name->second; break; }
                if (path.name.empty()) path.name = pathFallback[p];
                path.super = p == 2 && middleSuper ? 20 : 10;
                bool first = true;
                for (const auto& entry : options) if (entry.group == 3 && entry.source == sources[p]) {
                    path.perks.push_back(entry.choice.name);
                    if (first && entry.choice.entry != 20) { path.melee = entry.choice.entry; first = false; }
                }
                if (first || path.super >= options.size()) continue;
                item.abilities[3].push_back(options[path.melee].choice);
                if (std::none_of(item.abilities[2].begin(), item.abilities[2].end(), [&](const auto& choice) { return choice.entry == path.super; }))
                    item.abilities[2].push_back(options[path.super].choice);
                item.paths.push_back(std::move(path));
            }
        }
    }
    result.finish();
    output = std::move(result);
    return true;
}
bool sweep_icons(const Catalog& catalog, std::vector<IconRow>& icons, std::vector<std::string>& packages,
                 std::vector<IconOwner>& owners) {
    icons.clear();
    packages.clear();
    owners.clear();
    if (catalog.iconClass == 0) return false;
    ReadScope scope;
    core::path::Buffer directory{};
    if (!packages::package_directory(directory) || !packages::collect_keys(scope.keys)) return false;
    IconSweep sweep{&icons, &packages, &catalog.investmentIconRows, {}, false};
    reader::ScanResult scan{};
    const bool complete =
        reader::scan_class_entries(directory.chars.data(), catalog.iconClass, &visit_icon, &sweep, scan);
    // The interface packages keep their art as bare images, with no container to be swept for. A
    // package that has containers is left out of that pass, since its bare images are their layers.
    // The containers stand on their own, so a pass that stops short only leaves some images out.
    if (complete && catalog.imageClass != 0) {
        try {
            for (const auto& [family, index] : sweep.families) sweep.containerPackages.insert(family);
            sweep.bare = true;
            (void)reader::scan_class_entries(directory.chars.data(), catalog.imageClass, &visit_icon, &sweep, scan);
        } catch (...) {
            // The images found before the throw are kept, as a pass that stops short keeps them.
        }
    }
    // The owners only name what uses an icon, so a sweep that finds none of them still stands.
    try {
        const reader::Source source{directory.chars.data(), &scope.keys};
        collect_icon_owners(catalog, source, *scope.scratch, owners);
    } catch (...) {
        owners.clear();
    }
    reader::release_caches();
    if (!complete && !sweep.overflowed) {
        icons.clear();
        packages.clear();
        return false;
    }
    sort_icons(icons);
    return true;
}
}
