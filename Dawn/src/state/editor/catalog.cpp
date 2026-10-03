// SPDX-License-Identifier: GPL-3.0-only
// Selection scopes follow Sundial by KyleThmpsn. See vendor/sundial/NOTICE.md.
#include "catalog.h"
#include "../../../vendor/sundial/dummy_items.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

namespace dawn::state::editor {
/** @return The curve for one stat row inside a group, or null when the group does not scale it. */
const ScaledStat* scaled_stat(const Catalog& catalog, std::uint16_t groupIndex, std::uint16_t statRow) noexcept {
    if (groupIndex >= catalog.statGroups.size()) return nullptr;
    for (const auto& scaled : catalog.statGroups[groupIndex].scaled) {
        if (scaled.definitionIndex == statRow) return &scaled;
    }
    return nullptr;
}

std::int32_t display_stat(const Catalog& catalog, std::uint16_t groupIndex, std::uint16_t statRow,
                          std::int32_t investment) noexcept {
    return display_stat(scaled_stat(catalog, groupIndex, statRow), investment);
}

std::int32_t display_stat(const ScaledStat* scaled, std::int32_t investment) noexcept {
    if (!scaled || scaled->curve.empty()) return investment;
    const auto& curve = scaled->curve;
    // An authored point wins outright; the curve is a lookup before it is an interpolation.
    for (const auto& point : curve) if (point.investment == investment) return point.display;
    // A linear stat keeps whatever the curve does not name, rather than clamping to an endpoint.
    if (scaled->linear) return investment;
    if (investment < curve.front().investment) return curve.front().display;
    if (investment > curve.back().investment) return curve.back().display;
    for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
        const auto& left = curve[i];
        const auto& right = curve[i + 1];
        if (investment < left.investment || investment > right.investment) continue;
        const std::int64_t span = std::int64_t(right.investment) - left.investment;
        if (span == 0) return left.display;
        const std::int64_t rise = std::int64_t(right.display) - left.display;
        const std::int64_t run = std::int64_t(investment) - left.investment;
        return static_cast<std::int32_t>(left.display + ((rise * run) / span));
    }
    return investment;
}

bool numeric_stat(const Catalog& catalog, std::uint16_t groupIndex, std::uint16_t statRow) noexcept {
    const auto* scaled = scaled_stat(catalog, groupIndex, statRow);
    return scaled && scaled->numeric;
}

/**
 * @return The damage type one item deals, or none when it carries no damage marker.
 * The installed build marks a weapon's damage type with a sandbox perk rather than a field. Six
 * indices name the fixed markers: an older trio and the one the modern sandbox uses. A weapon
 * carrying markers for more than one damage type switches at runtime, so it reports none.
 * @param detail Item detail carrying the sandbox perk list.
 */
DamageType damage_type_of(const build_data::items::details::Definition& detail) noexcept {
    constexpr std::uint16_t kLegacyArc = 83, kLegacySolar = 84, kLegacyVoid = 85;
    constexpr std::uint16_t kModernArc = 449, kModernSolar = 450, kModernVoid = 451;
    DamageType found = DamageType::none;
    const std::size_t count = (std::min)(static_cast<std::size_t>(detail.sandboxPerkCount),
                                         detail.sandboxPerks.size());
    for (std::size_t i = 0; i < count; ++i) {
        DamageType marker = DamageType::none;
        switch (detail.sandboxPerks[i]) {
        case kLegacyArc: case kModernArc: marker = DamageType::arc; break;
        case kLegacySolar: case kModernSolar: marker = DamageType::solar; break;
        case kLegacyVoid: case kModernVoid: marker = DamageType::void_; break;
        default: continue;
        }
        // Two markers naming different damage types mean the weapon chooses at runtime.
        if (found != DamageType::none && found != marker) return DamageType::none;
        found = marker;
    }
    return found;
}

std::string searchable(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
bool matches(const CatalogItem& item, const std::string& query) {
    std::size_t begin = 0;
    while (begin < query.size()) {
        const auto end = query.find(' ', begin);
        const auto word = query.substr(begin, end == std::string::npos ? end : end - begin);
        if (!word.empty() && item.search.find(word) == std::string::npos) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}
bool fits_class(const CatalogItem& item, CharacterClass characterClass) noexcept {
    return item.characterClass == 3 || item.characterClass == static_cast<std::uint8_t>(characterClass);
}
const CatalogItem* Catalog::find(std::uint32_t hash) const noexcept {
    const auto it = hashes.find(hash);
    return it == hashes.end() ? nullptr : &items[it->second];
}
const CatalogItem* Catalog::index(std::uint16_t id) const noexcept {
    const auto it = indices.find(id);
    return it == indices.end() ? nullptr : &items[it->second];
}
namespace {
/** The socket type a weapon's shader sits in, and the one cosmetic socket type whose pools name no marker. */
constexpr std::uint32_t kShaderSocketType = 180;
constexpr std::uint32_t kUnmarkedCosmeticSocketType = 746;
void unique(std::vector<std::uint16_t>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}
/** @return The subtype the scopes group an item by: what the game names it, such as Auto Rifle, or else its bucket. */
std::string item_subtype(const CatalogItem& item) {
    return item.type.empty() ? "Bucket " + std::to_string(item.definition.bucketId) : item.type;
}
/** @return The type the widest gear scope groups an item by: every weapon, every armor piece, or else its bucket. */
std::uint64_t item_type(const CatalogItem& item) {
    constexpr std::uint64_t kKind = 1ULL << 32U;
    return item.kind == GearKind::weapon ? kKind | 1U : item.kind == GearKind::armor ? kKind | 2U : item.definition.bucketId;
}
/**
 * @return True for a plug that only dresses an item: a shader, ornament, transmat or projection, which
 * the catalog files as cosmetic by its type, a tracker, or one of the defaults a cosmetic socket holds.
 */
bool cosmetic_plug(const CatalogItem& plug) {
    return plug.kind == GearKind::cosmetic || plug.name == "Default Shader" || plug.name == "Default Ornament"
        || plug.name == "Tracker Disabled" || plug.name.find("Kill Tracker") != std::string::npos
        || searchable(plug.type).find("tracker") != std::string::npos;
}
/** @return True when one of an item's sockets is cosmetic: it offers a cosmetic plug, or its type is the cosmetic one that names none. */
bool cosmetic_lane(const Catalog& catalog, const CatalogItem& item, std::size_t lane) {
    if (item.detail.socketTypes[lane] == kUnmarkedCosmeticSocketType) return true;
    return std::any_of(item.compatible[lane].begin(), item.compatible[lane].end(), [&catalog](std::uint16_t id) {
        const CatalogItem* plug = catalog.index(id);
        return plug != nullptr && cosmetic_plug(*plug);
    });
}
}
void Catalog::finish() {
    hashes.clear(); indices.clear(); socketPools.clear(); subtypeSocketPools.clear(); subtypePools.clear(); typePools.clear();
    plugs.clear();
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto& item = items[i];
        hashes[item.definition.definitionHash] = i;
        indices[item.definition.definitionIndex] = i;
        char hash[40]{};
        std::snprintf(hash, sizeof hash, "0x%08X %u", item.definition.definitionHash, item.definition.definitionHash);
        item.internal = item.internal || item.name.empty()
            || ((item.kind == GearKind::weapon || item.kind == GearKind::armor) && item.type.empty())
            || dummies::contains(item.definition.definitionHash);
        if (item.name.empty()) { item.unnamed = true; item.name = std::string(item.plug ? "Unnamed perk " : "Unnamed item ") + hash; }
        item.search = searchable(item.name + " " + item.type + " " + item.description + " " + hash);
        if (item.plug) plugs.push_back(item.definition.definitionIndex);
        for (std::size_t lane = 0; lane < item.detail.ordinarySocketCount; ++lane) unique(item.compatible[lane]);
    }
    // A cosmetic socket's plugs stay out of the gear pools, so a weapon's gear scope does not fill with
    // every shader, ornament and tracker in the game. A cosmetic socket draws on them still.
    std::unordered_set<std::uint16_t> cosmetic;
    for (const auto& item : items)
        for (std::size_t lane = 0; lane < item.detail.ordinarySocketCount; ++lane)
            if (cosmetic_lane(*this, item, lane)) cosmetic.insert(item.compatible[lane].begin(), item.compatible[lane].end());
    for (const auto& item : items) {
        const std::string subtype = item_subtype(item);
        auto& sameSubtype = subtypePools[subtype];
        auto& sameType = typePools[item_type(item)];
        for (std::size_t lane = 0; lane < item.detail.ordinarySocketCount; ++lane) {
            const auto& pool = item.compatible[lane];
            const auto type = item.detail.socketTypes[lane];
            if (type != build_data::items::details::kUnavailableSocketType) {
                auto& socket = socketPools[type];
                socket.insert(socket.end(), pool.begin(), pool.end());
                auto& combined = subtypeSocketPools[{subtype, type}];
                combined.insert(combined.end(), pool.begin(), pool.end());
            }
            for (const auto id : pool) {
                if (!cosmetic.contains(id)) {
                    sameSubtype.push_back(id);
                    sameType.push_back(id);
                }
            }
            // Pools also expose unnamed/internal plugs that declare no category of their own.
            plugs.insert(plugs.end(), pool.begin(), pool.end());
        }
    }
    // An older weapon carries no damage marker of its own and takes its damage type from a plug it
    // comes with, as Sundial reads it. Plugs naming different damage types make a weapon that
    // switches, so it keeps none.
    for (auto& item : items) {
        if (item.kind != GearKind::weapon || item.damageType != DamageType::none) continue;
        DamageType plugged = DamageType::none;
        bool mixed = false;
        for (std::size_t lane = 0; lane < item.detail.ordinarySocketCount; ++lane) {
            const CatalogItem* plug = item.detail.initialPlugIndices[lane] != build_data::items::details::kUnavailableItemIndex
                ? index(item.detail.initialPlugIndices[lane]) : nullptr;
            if (plug == nullptr || plug->damageType == DamageType::none) continue;
            mixed = mixed || (plugged != DamageType::none && plugged != plug->damageType);
            plugged = plug->damageType;
        }
        if (!mixed && plugged != DamageType::none) { item.damageType = plugged; item.damageTypeFromPlug = true; }
    }
    for (auto& [key, values] : socketPools) { (void)key; unique(values); }
    // Shaders are shared across weapon families. A family whose stock items have no shader socket still
    // takes the installed shaders there, without being handed another family's traits.
    if (const auto shaders = socketPools.find(kShaderSocketType); shaders != socketPools.end() && !shaders->second.empty())
        for (const auto& item : items)
            if (item.kind == GearKind::weapon) (void)subtypeSocketPools.try_emplace({item_subtype(item), kShaderSocketType}, shaders->second);
    for (auto& [key, values] : subtypeSocketPools) { (void)key; unique(values); }
    for (auto& [key, values] : subtypePools) { (void)key; unique(values); }
    for (auto& [key, values] : typePools) { (void)key; unique(values); }
    unique(plugs);
    for (auto id : plugs) if (auto it = indices.find(id); it != indices.end()) items[it->second].plug = true;
}
std::vector<std::uint16_t> Catalog::candidates(const CatalogItem& item, std::size_t lane, PlugScope scope) const {
    if (lane >= item.detail.ordinarySocketCount || lane >= item.compatible.size()) return {};
    if (scope == PlugScope::all) return plugs;
    if (scope == PlugScope::compatible) return item.compatible[lane];
    const auto type = item.detail.socketTypes[lane];
    const bool typed = type != build_data::items::details::kUnavailableSocketType;
    if (scope == PlugScope::subtype || scope == PlugScope::itemType) {
        std::vector<std::uint16_t> options;
        if (scope == PlugScope::subtype) {
            if (const auto same = subtypePools.find(item_subtype(item)); same != subtypePools.end()) options = same->second;
        } else if (const auto same = typePools.find(item_type(item)); same != typePools.end()) {
            options = same->second;
        }
        // A cosmetic socket keeps the cosmetics its own type offers, which the gear pools leave out.
        if (const auto socket = socketPools.find(type); typed && socket != socketPools.end() && cosmetic_lane(*this, item, lane)) {
            options.insert(options.end(), socket->second.begin(), socket->second.end());
            unique(options);
        }
        return options;
    }
    if (!typed) return item.compatible[lane];
    if (scope == PlugScope::socket) {
        const auto it = socketPools.find(type);
        return it == socketPools.end() ? std::vector<std::uint16_t>{} : it->second;
    }
    const auto it = subtypeSocketPools.find({item_subtype(item), type});
    return it == subtypeSocketPools.end() ? std::vector<std::uint16_t>{} : it->second;
}
} // namespace dawn::state::editor
