// SPDX-License-Identifier: GPL-3.0-only
#include "card.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "art.h"
#include "controls.h"
#include "internal.h"
#include "preview.h"
#include "tooltip.h"
#include "state/account/inventory/placement.h"

namespace dawn::core::ui::modules::loadout::card {
namespace {

using scaling::dpi::pixels;
namespace inv = state::account::inventory;

/** Card geometry: an item band over the fields that edit the item. */
constexpr float kPadding = 6.0F;
/** The control row is set shorter than a page control: one text line with a sliver over it. */
constexpr float kControlPaddingY = 1.0F;
constexpr float kControlTextScale = 0.88F;
/** Id of the card's context menu. */
constexpr const char* kMenuId = "card_menu";
/** 52 authored pixels hold a four-figure power without the field reading as an empty box. */
constexpr float kPowerFieldWidth = 52.0F;
/** Socket icons and the gap between them. */
constexpr float kPlugExtent = 24.0F;
constexpr float kPlugGap = 3.0F;
/** A picked card's mark: the game's tick on the accent, set into the band's far corner. */
constexpr float kPickMarkExtent = 14.0F;
constexpr float kPickMarkInset = 4.0F;
/** Share of the mark its tick leaves clear round itself. */
constexpr float kPickTickInset = 0.18F;
/**
 * The game's own tick and its small right-pointing arrow, bare images in the interface package
 * that no record points at, so they are named by their tags.
 */
constexpr std::uint32_t kTickTag = 0x80B46BB0U;
constexpr std::uint32_t kSubmenuArrowTag = 0x80B46D15U;
/** A submenu's arrow is fitted into this share of the text line, well under the words beside it. */
constexpr float kSubmenuArrowShare = 0.45F;
/** The card menu's row that opens the characters an item can be sent to. */
constexpr const char* kSendLabel = "Send To";
/** The two lock labels. The button offers the action, so its label is what pressing it does. */
constexpr const char* kLockLabel = "Lock";
constexpr const char* kUnlockLabel = "Unlock";

/** @return True when a card here can be picked for an action on many items: only the inventory page picks. */
[[nodiscard]] bool pickable() noexcept {
    return internal::model().view == internal::View::characterInventory;
}

/**
 * Sets a picked card's tick into the far corner of its band. It is drawn from inside the card, after
 * the band: a child draws over its page, so a tick the page drew there was covered by the band.
 */
void draw_pick_tick() noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 lo = ImGui::GetWindowPos();
    const ImVec2 hi{lo.x + ImGui::GetWindowSize().x, lo.y + ImGui::GetWindowSize().y};
    const float mark = pixels(kPickMarkExtent);
    const ImVec2 at{hi.x - mark - pixels(kPickMarkInset), lo.y + pixels(kPickMarkInset)};
    draw->PushClipRect(lo, hi, false);
    draw->AddRectFilled(at, {at.x + mark, at.y + mark}, ImGui::GetColorU32(ImGuiCol_CheckMark), pixels(controls::kCardRounding));
    const float inset = mark * kPickTickInset;
    (void)preview::draw_fitted(kTickTag, {at.x + inset, at.y + inset}, {mark - (inset * 2.0F), mark - (inset * 2.0F)},
                               pixels(1.0F), 1.0F, ImGui::GetColorU32(ImGuiCol_Text));
    draw->PopClipRect();
}

/** Outlines a picked card in the accent, from the page, so the outline's outer half is not cut off. */
void draw_pick_outline(ImVec2 lo, ImVec2 hi) noexcept {
    ImGui::GetWindowDrawList()->AddRect(
        lo, hi, ImGui::GetColorU32(ImGuiCol_CheckMark), pixels(controls::kCardRounding), 0, pixels(controls::kRailWidth));
}

/** @return How many sockets one owned item shows on its card, or zero when it has none. */
[[nodiscard]] std::size_t plug_count(const edit::Item* item) noexcept {
    if (item == nullptr) {
        return 0;
    }
    const edit::Catalog& catalog = internal::model().catalog;
    edit::Item resolved = *item;
    if (!edit::materialize(resolved, catalog)) {
        return 0;
    }
    // The count has to agree with the row, which leaves out the unnamed stat plugs.
    std::size_t shown = 0;
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        const edit::CatalogItem* fitted = current ? catalog.find(*current) : nullptr;
        shown += fitted == nullptr || !fitted->unnamed ? 1 : 0;
    }
    return shown;
}

/**
 * @return The content width a card of this outer width offers.
 * The height and the socket wrap are both taken from this, so the height a page reserves and the
 * rows the card actually draws can never disagree.
 * @param width Outer card width in framebuffer pixels.
 */
[[nodiscard]] float inner_width(float width) noexcept {
    return (std::max)(
        0.0F, width - (2.0F * ImGui::GetStyle().ChildBorderSize) - (2.0F * pixels(kPadding)));
}

/** @return How many sockets fit across one card, at least one. */
[[nodiscard]] std::size_t plugs_per_row(float inner) noexcept {
    const float pitch = pixels(kPlugExtent) + pixels(kPlugGap);
    if (pitch <= 0.0F) {
        return 1;
    }
    const auto fit = static_cast<std::size_t>((inner + pixels(kPlugGap)) / pitch);
    return (std::max)(std::size_t{1}, fit);
}

/** @return How many rows the sockets of one item wrap onto. */
[[nodiscard]] std::size_t plug_rows(std::size_t count, float inner) noexcept {
    if (count == 0) {
        return 0;
    }
    const std::size_t perRow = plugs_per_row(inner);
    return (count + perRow - 1) / perRow;
}

/**
 * Sends the armory to what can fill one equipment slot: its category, narrowed to that slot, as
 * the game opens a bucket on the items that go in it. The armory carries no subclasses, so the
 * subclass goes to the character page, where its picker is.
 */
void browse_for_slot(std::size_t slot) noexcept {
    internal::Model& state = internal::model();
    if (slot == internal::kSubclassSlot) {
        state.view = internal::View::characters;
        return;
    }
    state.view = internal::View::armory;
    state.browse.category = slot <= internal::kLastWeaponSlot  ? internal::Category::weapons
                            : slot <= internal::kLastArmorSlot ? internal::Category::armor
                                                               : internal::Category::cosmetics;
    state.browse.slot = static_cast<int>(slot);
    state.browse.type.clear();
}

/**
 * Draws the sockets of one item as icons, each opening its picker.
 * Armor carries more lanes than a card is wide, so the row wraps at the same count the height was
 * reserved for rather than running off the edge of the card and being clipped.
 */
void draw_plug_row(const edit::CatalogItem& definition,
                   const edit::Item& item,
                   float inner) noexcept {
    const edit::Catalog& catalog = internal::model().catalog;
    edit::Item resolved = item;
    if (!edit::materialize(resolved, catalog)) {
        return;
    }
    const float plug = pixels(kPlugExtent);
    const std::size_t perRow = plugs_per_row(inner);
    std::size_t shown = 0;
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        const edit::CatalogItem* fitted = current ? catalog.find(*current) : nullptr;
        // Armor's rolled stat plugs have no name and no artwork; the stat bars edit those.
        if (fitted != nullptr && fitted->unnamed) {
            continue;
        }
        ImGui::PushID(static_cast<int>(lane));
        if (shown++ % perRow != 0) {
            ImGui::SameLine(0.0F, pixels(kPlugGap));
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("plug", {plug, plug});
        // Sockets are badged as the tooltip badges them, so a trait reads as a trait on the card.
        tooltip::draw_plug_badge(fitted, at, plug);
        if (ImGui::IsItemHovered()) {
            if (fitted != nullptr) {
                tooltip::draw_plug(*fitted);
            } else {
                ImGui::SetTooltip("Empty Socket");
            }
        }
        ImGui::PopID();
        if (clicked) {
            // The picker edits whichever item the inspector holds, so the card selects it first.
            internal::select(definition, item.instanceSoid);
            internal::open_perk_picker(definition, lane);
        }
    }
}

/**
 * Draws the item's band and leaves the cursor directly under it.
 * The icon is struck against the card's own frame, so it has no padding above, below or to its
 * left, and the band reads as one solid header rather than a picture floating inside one.
 * @param label Where the item is equipped, or the bucket it fills. An empty slot has only this.
 * @param locked True for a locked item, which the band marks with the game's padlock.
 * @return The band's bottom-right corner in screen space, which is also the hover target.
 */
ImVec2 draw_band(const edit::CatalogItem* definition, const char* label, bool locked) noexcept {
    const float band = art::band_height();
    const float border = ImGui::GetStyle().ChildBorderSize;
    const ImVec2 card = ImGui::GetWindowPos();
    const ImVec2 origin{card.x + border, card.y + border};
    const float right = card.x + ImGui::GetWindowSize().x - border;
    // A click anywhere on a card opens it, so the band lightens under the pointer as a row does.
    art::item_band(definition, label, origin, right, definition != nullptr && ImGui::IsWindowHovered(), locked);

    // The content cursor sits one padding below the frame, so the spacer covers what is left of
    // the band and the next item starts against its bottom edge.
    ImGui::Dummy({right - origin.x, (std::max)(0.0F, border + band - pixels(kPadding))});
    return {right, origin.y + band};
}

/** @return The width the lock toggle takes, which is the wider of its two labels. */
[[nodiscard]] float lock_toggle_width() noexcept {
    // Both labels are measured, so the row does not shift when the state flips under the pointer.
    return (std::max)(ImGui::CalcTextSize(kLockLabel).x, ImGui::CalcTextSize(kUnlockLabel).x)
           + (ImGui::GetStyle().FramePadding.x * 2.0F);
}

/** Draws the lock toggle. */
void draw_lock_toggle(edit::Item& item) noexcept {
    const bool locked = (item.flags & inv::kLockedItemFlag) != 0;
    const float width = lock_toggle_width();
    if (ImGui::Button(locked ? kUnlockLabel : kLockLabel, {width, ImGui::GetFrameHeight()})) {
        item.flags = locked ? item.flags & ~inv::kLockedItemFlag
                            : item.flags | inv::kLockedItemFlag;
        internal::mark_changed();
    }
}

/** @return True when the game gives this item a Power, which only gear carries. */
[[nodiscard]] bool carries_power(const edit::CatalogItem& definition) noexcept {
    return definition.kind == edit::GearKind::weapon || definition.kind == edit::GearKind::armor;
}

/** @return The height of the card's control row, which is set shorter than a page control. */
[[nodiscard]] float control_height() noexcept {
    // Measured from the size the row pushes, which is what its controls are drawn at. Taking the
    // scaled font size instead counted the display scale twice and left every card a tall chin.
    return (ImGui::GetStyle().FontSizeBase * kControlTextScale) + (pixels(kControlPaddingY) * 2.0F);
}

/** Sets the control row's type and frame padding, for as long as the value lives. */
struct ControlRowStyle {
    ControlRowStyle() noexcept {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kControlTextScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, pixels(kControlPaddingY)});
    }
    ~ControlRowStyle() {
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    ControlRowStyle(const ControlRowStyle&) = delete;
    ControlRowStyle& operator=(const ControlRowStyle&) = delete;
};

/** Draws the one row of controls: power at the left, the lock and the card's action at the right. */
void draw_controls(const edit::CatalogItem& definition,
                   edit::Item& item,
                   std::size_t slot,
                   Action action) noexcept {
    internal::Model& state = internal::model();
    const float padding = pixels(kPadding);
    const float row = ImGui::GetCursorScreenPos().y;
    // Every control on the row takes the row's own height: its type is set a little under the
    // body and the frame padding is set for it.
    const ControlRowStyle rowStyle;

    // An emblem or a ship has no Power, so it is offered no field to set one in.
    if (carries_power(definition)) {
        // The field speaks in Power; the account stores the level behind it. It holds the same
        // range every other Power field does, which also keeps the figure inside its four digits.
        ImGui::SetNextItemWidth(pixels(kPowerFieldWidth));
        int power = internal::power_of(item.level);
        const bool powerChanged = ImGui::InputInt("##power", &power, 0, 0);
        if (powerChanged) {
            item.level = internal::level_of(std::clamp(power, 0, internal::kPowerSliderMaximum));
        }
        internal::record_scalar_edit(powerChanged);
        ImGui::SameLine(0.0F, 0.0F);
    }

    const char* title = action == Action::equip  ? "Equip"
                        : action == Action::pull ? "Pull"
                                                 : "Swap";
    const float button = ImGui::CalcTextSize(title).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    // The action is pinned to the right edge of the card, with the lock beside it, which keeps
    // every row reading alike however wide the grid drew them. Screen space, so nothing is
    // assumed about where a child window puts its local origin relative to its border and its
    // padding.
    const float edge = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x
                       - ImGui::GetStyle().ChildBorderSize - padding - button;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float lockLeft = edge - gap - lock_toggle_width();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({(std::max)(at.x, lockLeft), row});
    draw_lock_toggle(item);
    ImGui::SameLine(0.0F, gap);
    ImGui::SetCursorScreenPos({(std::max)(ImGui::GetCursorScreenPos().x, edge), row});
    if (!ImGui::Button(title, {button, ImGui::GetFrameHeight()})) {
        return;
    }
    if (action == Action::equip) {
        internal::record_edit(edit::equip(
            *state.draft, state.catalog, state.character, item.instanceSoid, state.status));
    } else if (action == Action::pull) {
        internal::record_edit(edit::pull_from_postmaster(
            *state.draft, state.catalog, state.character, item.instanceSoid, state.status));
    } else {
        browse_for_slot(slot);
    }
}

/** Offers the other characters one stowed item can be sent to, whatever their class. */
void draw_send_menu(const edit::Item& item) noexcept {
    const internal::Model& state = internal::model();
    const state::AccountState& account = state.draft->after;
    if (account.characterCount < 2) {
        return;
    }
    // Dear ImGui draws a submenu's arrow at the font's own size, heavier than the words around it.
    // Its label and arrow are drawn clear, and the label and the game's small arrow set in their
    // place, on a channel over the row's own hover fill as every other row's words are.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    const float arrow = line * kSubmenuArrowShare;
    const float right = at.x + ImGui::GetContentRegionAvail().x;
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    draw->AddText(at, text, kSendLabel);
    (void)preview::draw_fitted(kSubmenuArrowTag, {right - arrow, at.y + ((line - arrow) * 0.5F)}, {arrow, arrow},
                               pixels(1.0F), 1.0F, text);
    draw->ChannelsSetCurrent(0);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{});
    const bool open = controls::begin_submenu(kSendLabel);
    ImGui::PopStyleColor();
    draw->ChannelsMerge();
    if (!open) {
        return;
    }
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (index == state.character) {
            continue;
        }
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::MenuItem(internal::character_label(index).c_str())) {
            (void)internal::send_item(item.instanceSoid, index);
        }
        ImGui::PopID();
    }
    controls::end_submenu();
}

/**
 * Draws the card's context menu, which offers every action the card has as words: opening it in
 * the inspector, equipping or unequipping, locking, and removing a stowed item. A locked item
 * cannot be removed until it is unlocked, which is the game's own rule for the lock.
 */
void draw_menu(const edit::CatalogItem& definition,
               edit::Item& item,
               std::size_t slot,
               Action action) noexcept {
    if (!controls::begin_menu(kMenuId)) {
        return;
    }
    internal::Model& state = internal::model();
    const bool equipped = action == Action::swap;
    const bool locked = (item.flags & inv::kLockedItemFlag) != 0;
    if (ImGui::Selectable("Open")) {
        internal::select(definition, item.instanceSoid);
    }
    // Picking gathers items for one action on all of them; Ctrl and a click does the same.
    if (pickable() && ImGui::Selectable(internal::is_picked(item.instanceSoid) ? "Deselect" : "Select", false)) {
        internal::toggle_pick(item.instanceSoid);
    }
    if (equipped) {
        if (ImGui::Selectable("Unequip")) {
            internal::record_edit(edit::unequip(
                *state.draft, state.catalog, state.character, slot, state.status));
        }
        if (ImGui::Selectable("Swap")) {
            browse_for_slot(slot);
        }
    } else if (action == Action::pull) {
        // A postmaster item cannot be equipped, so the menu offers the pull in its place.
        if (ImGui::Selectable("Pull from Postmaster")) {
            internal::record_edit(edit::pull_from_postmaster(
                *state.draft, state.catalog, state.character, item.instanceSoid, state.status));
        }
    } else {
        if (ImGui::Selectable("Equip")) {
            internal::record_edit(edit::equip(
                *state.draft, state.catalog, state.character, item.instanceSoid, state.status));
        }
        draw_send_menu(item);
    }
    if (ImGui::Selectable(locked ? kUnlockLabel : kLockLabel)) {
        item.flags = locked ? item.flags & ~inv::kLockedItemFlag : item.flags | inv::kLockedItemFlag;
        internal::mark_changed();
    }
    // Removal asks first, as the pane's own removal does. With live apply on, an edit reaches the
    // game the moment it is made, so a menu row that deleted at once left nothing to catch a slip.
    // A row that cannot be taken stays, disabled, with the reason under the pointer.
    const bool removable = !equipped && !locked;
    if (ImGui::Selectable("Remove", false, removable ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)) {
        internal::request_removal(item.instanceSoid);
    }
    if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", equipped ? "Unequip it to remove it." : "Unlock it to remove it.");
    }
    controls::end_menu();
}

} // namespace

float natural_height(const edit::Item* item, float width) noexcept {
    const ImGuiStyle& style = ImGui::GetStyle();
    // The band is flush with the frame, so it carries the top border rather than a padding.
    float total = style.ChildBorderSize + art::band_height();
    total += style.ItemSpacing.y + control_height();
    const std::size_t rows = plug_rows(plug_count(item), inner_width(width));
    total += static_cast<float>(rows) * (style.ItemSpacing.y + pixels(kPlugExtent));
    // The control row's type is scaled, so its height lands on a fraction; the card rounds up
    // rather than clipping the last row by that fraction.
    return std::ceil(total + pixels(kPadding) + style.ChildBorderSize);
}

void draw(edit::Item* item,
          const char* label,
          std::size_t slot,
          Action action,
          float width,
          float cardHeight) noexcept {
    internal::Model& state = internal::model();
    const edit::CatalogItem* definition =
        item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
    const float padding = pixels(kPadding);
    // Taken now: an unequip from the card's own menu moves the item before the outlines below read it.
    const std::uint64_t instance = item != nullptr ? item->instanceSoid : 0;

    ImGui::PushID(static_cast<int>(slot));
    ImGui::PushID(static_cast<int>(instance));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padding, padding});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, pixels(controls::kCardRounding));
    // A card is sized to its content by the page, so it never scrolls: a stray pixel of overflow
    // must not grow a scrollbar over the action pinned to its right edge.
    if (ImGui::BeginChild("card",
                          {width, cardHeight},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 bandOrigin = ImGui::GetWindowPos();
        const ImVec2 bandCorner = draw_band(definition, label, item != nullptr && (item->flags & inv::kLockedItemFlag) != 0);

        if (definition == nullptr) {
            // Set as the control row it stands in for, so its label sits inside the row's height.
            const ControlRowStyle rowStyle;
            if (ImGui::Button("Choose Item", {-FLT_MIN, control_height()})) {
                browse_for_slot(slot);
            }
        } else {
            // The whole band is the hover target, not only the name text inside it.
            if (ImGui::IsMouseHoveringRect(bandOrigin, bandCorner) && ImGui::IsWindowHovered()) {
                tooltip::draw(*definition, item);
            }
            draw_controls(*definition, *item, slot, action);
            draw_plug_row(*definition, *item, inner_width(width));
            // The card as a whole opens the item in the inspector, or with Ctrl held picks it. This
            // is asked after its own controls have been submitted, so a click that landed on the
            // lock, the swap, the level field or a socket is already accounted for.
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (ImGui::GetIO().KeyCtrl && pickable()) {
                    internal::toggle_pick(item->instanceSoid);
                } else {
                    internal::select(*definition, item->instanceSoid);
                }
            }
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(kMenuId);
            }
            draw_menu(*definition, *item, slot, action);
        }
        if (definition != nullptr && pickable() && internal::is_picked(instance)) {
            draw_pick_tick();
        }
    }
    ImGui::EndChild();
    // The card the inspector holds is outlined in white, as the armory outlines its own selected
    // row, so the page says which item the pane beside it is editing. A picked card is marked in
    // the accent instead, so the set being gathered reads apart from the one card the pane holds.
    if (definition != nullptr && pickable() && internal::is_picked(instance)) {
        draw_pick_outline(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
    if (definition != nullptr && state.selection.holds(definition->definition.definitionHash, instance)) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                            ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(ImGuiCol_Text),
                                            pixels(controls::kCardRounding),
                                            0,
                                            pixels(controls::kRailWidth));
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::PopID();
}

} // namespace dawn::core::ui::modules::loadout::card
