// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <optional>
#include <cstdio>
#include <imgui.h>
#include <map>
#include <string>
#include <vector>

#include "../../../scaling/dpi/ui_dpi_scaling.h"
#include "../art.h"
#include "../controls.h"
#include "../internal.h"
#include "../tooltip.h"

namespace dawn::core::ui::modules::loadout::internal {
namespace {

using scaling::dpi::pixels;

/** One scope row per PlugScope value. */
constexpr std::size_t kScopeCount = static_cast<std::size_t>(edit::PlugScope::all) + 1;
/** Warning shown for every scope past the compatible one. */
constexpr const char* kExpandedScopeWarning = "May include perks this item does not support.";
/** Stronger warning for the unrestricted scope. */
constexpr const char* kUnrestrictedScopeWarning =
    "Every discovered perk; some combinations stop it loading.";

/** 160 bytes hold the sheet's muted line: the socket, and the name of the perk in it now. */
constexpr std::size_t kSocketLineCapacity = 160;
/**
 * The scope selector at the end of the filter row: as wide as this at least, and wider when one of
 * its rows needs it, with room kept at its end for the chevron.
 */
constexpr float kScopeWidth = 190.0F;
constexpr float kScopeChevronRoom = 28.0F;
/**
 * The second filter row: type, rarity and order selectors, then the internal-plug toggle. The type
 * selector takes what the others leave, and keeps at least this however narrow the sheet is drawn.
 */
constexpr float kTypeFilterMinimumWidth = 120.0F;
constexpr float kRarityFilterWidth = 110.0F;
constexpr float kSortWidth = 110.0F;
constexpr const char* kSortLabels[]{"By Type", "Name A-Z", "Rarity"};
static_assert(std::size(kSortLabels) == static_cast<std::size_t>(Sort::count),
              "Every sort order needs a label.");
constexpr const char* kAnyRarityLabel = "All Rarities";
/** The toggle that offers the plugs the catalog marks internal, and what those are. */
constexpr const char* kDummyItemsLabel = "Dummy Items";
constexpr const char* kInternalItemsTip = "Placeholder and test definitions the game never shows.";
/** 64 bytes hold a plug type with its count. */
constexpr std::size_t kTypePreviewCapacity = 64;

/** Shown when a picked plug is not one the socket will take in the chosen scope. */
constexpr const char* kPerkRefused = "That perk cannot go in this socket.";
/** Title of the picker, used by both the open call and the modal. */
constexpr const char* kPickerTitle = "Choose a Perk";

/** The plugs the picker offers after its filters, and what each type would have shown. */
struct Matches {
    std::vector<const edit::CatalogItem*> options;
    /** Plugs each type would show, counted before the type filter so the type picker can say. */
    std::map<std::string, std::size_t> types;
    std::size_t total{};
};

/** @return Every offered plug passing the picker's filters, in its chosen order. */
[[nodiscard]] Matches matching_plugs() noexcept {
    Model& state = model();
    const SocketPicker& picker = state.picker;
    const std::string query = edit::searchable(picker.search);
    // A plug the native registry treats as inert is offered only under the unrestricted scope,
    // where everything discovered is on the table.
    const bool everything = picker.scope == edit::PlugScope::all;
    Matches matches;
    for (const std::uint16_t id : picker.options) {
        const edit::CatalogItem* plug = state.catalog.index(id);
        if (plug == nullptr || (!everything && plug->inert)
            || (!picker.includeInternal && plug->internal)
            || (picker.rarity != 0 && plug->definition.tier != picker.rarity)
            || !edit::matches(*plug, query)) {
            continue;
        }
        ++matches.total;
        if (!plug->type.empty()) {
            ++matches.types[plug->type];
        }
        if (picker.type.empty() || plug->type == picker.type) {
            matches.options.push_back(plug);
        }
    }
    const Sort sort = picker.sort;
    std::sort(matches.options.begin(),
              matches.options.end(),
              [sort](const edit::CatalogItem* a, const edit::CatalogItem* b) {
                  if (sort == Sort::type && a->type != b->type) {
                      if (a->type.empty() != b->type.empty()) {
                          return !a->type.empty();
                      }
                      return a->type < b->type;
                  }
                  if (sort == Sort::rarity && a->definition.tier != b->definition.tier) {
                      return a->definition.tier > b->definition.tier;
                  }
                  return a->name < b->name;
              });
    return matches;
}

/**
 * Draws the second filter row: plug type, rarity, order, and the internal-plug toggle. Everything
 * after the type is measured, so the type takes what is left and the row ends at the sheet's edge,
 * as the search row over it does.
 */
void draw_filter_row(const Matches& matches) noexcept {
    SocketPicker& picker = model().picker;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float toggleWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x
                              + ImGui::CalcTextSize(kDummyItemsLabel).x;
    const float trailing = pixels(kRarityFilterWidth) + pixels(kSortWidth) + toggleWidth
                           + (style.ItemSpacing.x * 3.0F);
    const float typeWidth = (std::max)(pixels(kTypeFilterMinimumWidth),
                                       ImGui::GetContentRegionAvail().x - trailing);
    char preview[kTypePreviewCapacity]{};
    if (picker.type.empty()) {
        (void)std::snprintf(preview, sizeof preview, "All Types (%zu)", matches.total);
    } else {
        const auto found = matches.types.find(picker.type);
        (void)std::snprintf(preview, sizeof preview, "%s (%zu)", picker.type.c_str(),
                            found != matches.types.end() ? found->second : 0U);
    }
    if (controls::begin_picker("##perk_type", preview, typeWidth)) {
        char row[kTypePreviewCapacity]{};
        (void)std::snprintf(row, sizeof row, "All Types (%zu)", matches.total);
        if (controls::picker_row(row, picker.type.empty())) {
            picker.type.clear();
        }
        for (const auto& [type, count] : matches.types) {
            (void)std::snprintf(row, sizeof row, "%s (%zu)", type.c_str(), count);
            if (controls::picker_row(row, picker.type == type)) {
                picker.type = type;
            }
        }
        controls::end_picker();
    }
    ImGui::SameLine();
    if (controls::begin_picker("##perk_rarity",
                               picker.rarity == 0
                                   ? kAnyRarityLabel
                                   : art::tier_name(static_cast<std::uint8_t>(picker.rarity)),
                               pixels(kRarityFilterWidth))) {
        if (controls::picker_row(kAnyRarityLabel, picker.rarity == 0)) {
            picker.rarity = 0;
        }
        for (int tier = 1; tier <= static_cast<int>(art::kExoticTier); ++tier) {
            if (controls::picker_row(art::tier_name(static_cast<std::uint8_t>(tier)),
                                     picker.rarity == tier)) {
                picker.rarity = tier;
            }
        }
        controls::end_picker();
    }
    ImGui::SameLine();
    int sort = static_cast<int>(picker.sort);
    if (controls::picker("##perk_sort", sort, kSortLabels, static_cast<int>(std::size(kSortLabels)),
                         pixels(kSortWidth))) {
        picker.sort = static_cast<Sort>(sort);
    }
    ImGui::SameLine();
    (void)controls::checkbox(kDummyItemsLabel, &picker.includeInternal);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kInternalItemsTip);
    }
}

/** @return A name as a list names it: Barrels, Auto Rifles, Batteries, Gauntlets, Leg Armor. */
[[nodiscard]] std::string plural(const std::string& name) {
    if (name.ends_with("s") || name.ends_with("Armor")) {
        return name;
    }
    if (name.size() > 1 && name.back() == 'y') {
        const char before = name[name.size() - 2];
        if (before != 'a' && before != 'e' && before != 'i' && before != 'o' && before != 'u') {
            return name.substr(0, name.size() - 1) + "ies";
        }
    }
    return name + "s";
}

/**
 * @return What one socket holds, named for the kind of plug most of its own pool is, such as Barrel
 * or Weapon Mod, or empty when its plugs name no kind.
 */
[[nodiscard]] std::string socket_name(const edit::CatalogItem& definition, std::size_t lane) {
    const edit::Catalog& catalog = model().catalog;
    std::map<std::string, std::size_t> counts;
    if (lane < definition.compatible.size()) {
        for (const std::uint16_t id : definition.compatible[lane]) {
            const edit::CatalogItem* plug = catalog.index(id);
            if (plug != nullptr && !plug->type.empty()) {
                ++counts[plug->type];
            }
        }
    }
    const auto most = std::max_element(
        counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return most == counts.end() ? std::string() : most->first;
}

/**
 * @return The scope picker's rows for one socket of one item, in the order of the PlugScope values.
 * Each scope is named for what it keeps to, so the rows read as a ladder from the item outwards:
 * Chroma Rush Barrels, Auto Rifle Barrels, All Barrels, Auto Rifles, All Weapons, All. An item the
 * bank names nothing for keeps the plain Compatible for its own pool.
 */
[[nodiscard]] std::array<std::string, kScopeCount> scope_labels(const edit::CatalogItem& definition, std::size_t lane) {
    const std::string subtype = definition.type.empty() ? std::string("Item Subtype") : plural(definition.type);
    const std::string type = definition.kind == edit::GearKind::weapon     ? std::string("All Weapons")
                             : definition.kind == edit::GearKind::armor    ? std::string("All Armor")
                             : definition.kind == edit::GearKind::subclass ? std::string("All Subclasses")
                             : definition.type.empty()                     ? std::string("Item Type")
                                                                           : "All " + subtype;
    std::string own = definition.unnamed ? std::string("Compatible") : definition.name + " Perks";
    std::string inSubtype = "Socket and " + subtype;
    std::string anywhere = "Socket Type";
    if (const std::string socket = socket_name(definition, lane); !socket.empty()) {
        const std::string sockets = plural(socket);
        // A socket already named for the subtype, as a helmet's Helmet Armor Mod, is not named for it twice.
        const bool named =
            definition.type.empty() || edit::searchable(socket).find(edit::searchable(definition.type)) != std::string::npos;
        if (!definition.unnamed) {
            own = definition.name + " " + sockets;
        }
        inSubtype = named ? sockets : definition.type + " " + sockets;
        anywhere = "All " + sockets;
    }
    return {own, inSubtype, anywhere, subtype, type, "All"};
}

/** @return The scope picker's width: its own, or wider when one of its rows needs the room. */
[[nodiscard]] float scope_width(const std::array<std::string, kScopeCount>& labels) noexcept {
    float widest = 0.0F;
    for (const std::string& label : labels) {
        widest = (std::max)(widest, ImGui::CalcTextSize(label.c_str()).x);
    }
    return (std::max)(pixels(kScopeWidth), widest + ImGui::GetStyle().FramePadding.x + pixels(kScopeChevronRoom));
}

/** Draws the scope picker, at the width the caller set, and the warning the chosen scope earns. */
void draw_scope_row(const edit::CatalogItem& definition,
                    const std::array<std::string, kScopeCount>& labels,
                    float width) noexcept {
    Model& state = model();
    std::array<const char*, kScopeCount> rows{};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i] = labels[i].c_str();
    }
    int scope = static_cast<int>(state.picker.scope);
    if (controls::picker("##scope", scope, rows.data(), static_cast<int>(rows.size()), width)) {
        state.picker.scope = static_cast<edit::PlugScope>(scope);
        state.picker.options =
            state.catalog.candidates(definition, state.picker.lane, state.picker.scope);
    }
    if (state.picker.scope == edit::PlugScope::compatible) {
        return;
    }
    ImGui::TextColored(tooltip::pending(),
                       "%s",
                       state.picker.scope == edit::PlugScope::all ? kUnrestrictedScopeWarning
                                                                  : kExpandedScopeWarning);
}

/**
 * Draws one perk row and applies it when it is picked.
 * @param plug Offered plug.
 * @param item Owned item the plug would go into.
 * @return True when the plug was applied and the picker should close.
 */
[[nodiscard]] bool draw_perk_row(const edit::CatalogItem& plug,
                                 edit::Item& item,
                                 const std::optional<std::uint32_t>& current,
                                 float width,
                                 float rowHeight) noexcept {
    Model& state = model();
    ImGui::PushID(static_cast<int>(plug.definition.definitionIndex));
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner{origin.x + width, origin.y + rowHeight};
    const bool chosen = current && *current == plug.definition.definitionHash;
    // The row is the tooltip's own perk row with the plug's type under its name, and the plug's
    // own tooltip under the pointer: the same plug reads the same in the list as it does fitted.
    // Its fill runs the list's whole width from rule to rule, and its words keep the tooltip's inset.
    const bool clicked = ImGui::InvisibleButton("perk", {width, rowHeight});
    const bool hovered = ImGui::IsItemHovered();
    if (chosen || hovered) {
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(chosen ? ImGuiCol_Header : ImGuiCol_FrameBgHovered),
                            pixels(controls::kRowRounding));
    }
    // The fitted perk carries the rail a picker's chosen row carries, so it still reads apart from
    // a row that is only under the pointer.
    if (chosen) {
        draw->AddRectFilled(origin, {origin.x + pixels(controls::kRailWidth), corner.y}, ImGui::GetColorU32(ImGuiCol_Text));
    }
    // The rule is the row's own last line, so the fill of the row under it does not cover it.
    draw->AddLine({origin.x, corner.y - 1.0F}, {corner.x, corner.y - 1.0F}, ImGui::GetColorU32(tooltip::rule_color()));
    const float inset = tooltip::padding();
    ImGui::SetCursorScreenPos({origin.x + inset, origin.y});
    tooltip::draw_perk_row(&plug, (std::max)(0.0F, width - (inset * 2.0F)), tooltip::PerkDetail::typed);
    if (hovered) {
        tooltip::draw_plug(plug);
    }
    bool applied = false;
    if (clicked) {
        applied = edit::set_plug(item,
                                 state.catalog,
                                 state.picker.lane,
                                 plug.definition.definitionIndex,
                                 state.picker.scope);
        if (applied) {
            state.status = "Perk applied.";
            record_edit(true);
        } else {
            // Nothing is closed and nothing changes, so without a word the click looked ignored.
            // The sheet says it even when the same words stood on the bar as the picker opened.
            state.status = kPerkRefused;
            state.statusAtSheet.clear();
            record_edit(false);
        }
    }
    ImGui::PopID();
    return applied;
}

} // namespace

void open_perk_picker(const edit::CatalogItem& definition, std::size_t lane) noexcept {
    Model& state = model();
    state.picker.lane = lane;
    state.picker.search[0] = '\0';
    state.picker.type.clear();
    state.picker.rarity = 0;
    state.picker.options = state.catalog.candidates(definition, lane, state.picker.scope);
    // The popup is opened from the scope that submits it, which is the page window rather
    // than the card's child: a card that scrolls away or is culled must not take the modal with it.
    state.picker.requested = true;
}

void draw_perk_editor(const edit::CatalogItem& definition,
                      const edit::Item& item,
                      float width) noexcept {
    const edit::Catalog& catalog = model().catalog;
    // The instance carries the lanes it rolled; the definition's own defaults fill the rest.
    edit::Item resolved = item;
    if (!edit::materialize(resolved, catalog)) {
        return;
    }
    // The frame's own edges, inside its hairline, which each row's hit target spans as its fill does.
    const float border = ImGui::GetStyle().WindowBorderSize;
    const float left = ImGui::GetWindowPos().x + border;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - border;
    bool first = true;
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        const edit::CatalogItem* fitted = current ? catalog.find(*current) : nullptr;
        // The bank names nothing for armor's rolled stat plugs; those lanes are what the stat
        // bars above edit, so they are not listed as sockets the player would pick a perk for.
        if (fitted != nullptr && fitted->unnamed) {
            continue;
        }
        // Each socket is the tooltip's own perk row, as it is on the tooltip, with the plug's own
        // tooltip under the pointer, and the whole row, edge to edge and rule to rule, is the control
        // that opens the picker for that lane. The rows meet the rules between them.
        const float rowHeight = tooltip::perk_row_height(fitted, width, tooltip::PerkDetail::name);
        ImGui::PushID(static_cast<int>(lane));
        if (!first) {
            tooltip::draw_row_rule();
        }
        first = false;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({left, origin.y});
        const bool clicked = ImGui::InvisibleButton("socket", {right - left, rowHeight});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(origin);
        tooltip::draw_perk_row(fitted, width, tooltip::PerkDetail::name, hovered);
        if (hovered && fitted != nullptr) {
            tooltip::draw_plug(*fitted);
        }
        ImGui::PopID();
        if (clicked) {
            select(definition, item.instanceSoid);
            open_perk_picker(definition, lane);
        }
    }
}

void draw_perk_picker() noexcept {
    Model& state = model();
    if (state.picker.requested) {
        state.picker.requested = false;
        // What the bar says as the picker opens came before it, so the picker does not repeat it.
        state.statusAtSheet = state.status;
        ImGui::OpenPopup(kPickerTitle);
    }
    // The picker is a sheet on the tooltip's own ground, as the game's socket picker lays over the
    // inventory; the saved loadouts are drawn in the same one.
    if (!tooltip::begin_sheet(kPickerTitle)) {
        return;
    }
    edit::Item* item = selected_item();
    const edit::CatalogItem* definition =
        item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
    if (definition == nullptr) {
        ImGui::CloseCurrentPopup();
        tooltip::end_sheet();
        return;
    }
    // Escape closes a field's own list first, and a field being typed in keeps it.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::CloseCurrentPopup();
    }

    // The head names the item in its title cut and, under it, the socket and what is in it now.
    // A lane still on the definition's own default holds nothing in the instance, so what is in it
    // is read from the resolved item, as the tooltip and the pane both read it. Read from the
    // instance, such a lane was called empty and its perk was never marked as the one fitted.
    edit::Item resolved = *item;
    const std::optional<std::uint32_t> current =
        edit::materialize(resolved, state.catalog) ? resolved.sockets.plugs[state.picker.lane]
                                                   : item->sockets.plugs[state.picker.lane];
    const edit::CatalogItem* fitted = current ? state.catalog.find(*current) : nullptr;
    char socket[kSocketLineCapacity]{};
    (void)std::snprintf(socket,
                        sizeof socket,
                        "Socket %zu%s%s",
                        state.picker.lane + 1,
                        controls::kDetailSeparator,
                        fitted != nullptr ? fitted->name.c_str() : "Empty");
    tooltip::draw_sheet_head(definition->name, socket);
    controls::space(controls::kSectionSpacing);

    // Search leads the filter row; the scope sits at its end, with its warning under both.
    const std::array<std::string, kScopeCount> scopes = scope_labels(*definition, state.picker.lane);
    const float scopeWidth = scope_width(scopes);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - scopeWidth
                            - ImGui::GetStyle().ItemSpacing.x);
    (void)controls::search(
        "##perk_search", "Search Perks...", state.picker.search, sizeof state.picker.search);
    ImGui::SameLine();
    draw_scope_row(*definition, scopes, scopeWidth);

    controls::space(controls::kRowSpacing);
    const Matches matches = matching_plugs();
    draw_filter_row(matches);
    const std::vector<const edit::CatalogItem*>& options = matches.options;
    controls::space(controls::kRowSpacing);
    char count[32]{};
    (void)std::snprintf(
        count, sizeof count, options.size() == 1 ? "%zu Perk" : "%zu Perks", options.size());
    tooltip::draw_label(count);
    controls::space(controls::kRuleSpacing);
    ImGui::Separator();
    bool applied = false;
    // A typed row is one height whatever plug it holds, and the rows meet with no gap, so the
    // clipper pitches off the row alone.
    const float rowHeight = tooltip::perk_row_height(nullptr, 0.0F, tooltip::PerkDetail::typed);
    // Under the list: the item spacing after it, a row gap and the outcome line, so the line stands
    // on the sheet's bottom edge however many perks the list holds.
    const float below = ImGui::GetStyle().ItemSpacing.y + pixels(controls::kRowSpacing) + ImGui::GetTextLineHeight();
    const float listHeight = (std::max)(rowHeight, ImGui::GetContentRegionAvail().y - below);
    if (ImGui::BeginChild("perk_results", {0.0F, listHeight})) {
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ImGui::GetStyle().ItemSpacing.x, 0.0F});
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(options.size()), rowHeight);
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                applied |= draw_perk_row(
                    *options[static_cast<std::size_t>(i)], *item, current, width, rowHeight);
            }
        }
        ImGui::PopStyleVar();
        if (options.empty()) {
            ImGui::TextColored(tooltip::muted(), "No perks match.");
        }
    }
    ImGui::EndChild();
    controls::space(controls::kRowSpacing);
    // The sheet lies over the bar, so a perk the socket refuses is said here, where it was picked.
    draw_sheet_outcome();
    if (applied) {
        ImGui::CloseCurrentPopup();
    }
    tooltip::end_sheet();
}

} // namespace dawn::core::ui::modules::loadout::internal
