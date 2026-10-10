// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <imgui.h>

#include "../../../scaling/dpi/ui_dpi_scaling.h"
#include "../art.h"
#include "../internal.h"
#include "../controls.h"
#include "../tooltip.h"
#include "state/account/inventory/placement.h"

namespace dawn::core::ui::modules::loadout::internal {
namespace {

using scaling::dpi::pixels;
namespace inv = state::account::inventory;

/**
 * Width of a number field beside its label, in the grant and on an owned stack alike. The actions
 * the pane offers take the shared action height.
 */
constexpr float kGrantFieldWidth = 90.0F;
/** Title of the item removal, used for both the action and its modal. */
constexpr const char* kRemoveItemTitle = "Remove Item?";
/** The pane's own controls, which say what they do rather than showing a glyph. */
constexpr const char* kCloseLabel = "Close";
constexpr const char* kEquipLabel = "Equip";
constexpr const char* kUnequipLabel = "Unequip";
constexpr const char* kPullLabel = "Pull";
constexpr const char* kSendLabel = "Send To";
/** Why the removal waits, said under the pointer in the words the card's menu uses. */
constexpr const char* kUnequipToRemove = "Unequip it to remove it.";
constexpr const char* kUnlockToRemove = "Unlock it to remove it.";
/** Where the item the pane shows is kept, in the header's words. */
constexpr const char* kEquippedLabel = "Equipped";
constexpr const char* kPostmasterLabel = "Postmaster";
constexpr const char* kInInventoryLabel = "In Inventory";
constexpr const char* kNotInInventoryLabel = "Not in Inventory";

/** Where the selected item sits on the character, if it is equipped at all. */
struct Placement {
    bool equipped{};
    std::size_t slot{};
};

/** @return Where one owned instance sits in the character's equipment. */
[[nodiscard]] Placement placement_of(std::uint64_t instance) noexcept {
    const auto& slots = character().equipment.slots;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot] && slots[slot]->instanceSoid == instance) {
            return {true, slot};
        }
    }
    return {};
}

/**
 * @return Where the character already keeps a copy of one catalog item, in the header's words, or
 * "Not in Inventory" when it keeps none.
 * A pane bound to a catalog entry still offers to add another copy, so it stays bound to the entry;
 * this only lets its header say truthfully whether there is one already. Without it the header read
 * that it was not there beside an item just added to the inventory, and beside any already there.
 * @param hash Definition hash of the catalog item.
 */
[[nodiscard]] const char* holding_of(std::uint32_t hash) noexcept {
    const state::CharacterState& owner = character();
    for (const auto& slot : owner.equipment.slots) {
        if (slot && slot->definitionHash == hash) {
            return kEquippedLabel;
        }
    }
    bool postmaster = false;
    for (std::size_t i = 0; i < owner.inventory.count; ++i) {
        const edit::Item& stowed = owner.inventory.values[i];
        if (stowed.definitionHash != hash) {
            continue;
        }
        if (!stowed.postmaster) {
            return kInInventoryLabel;
        }
        postmaster = true;
    }
    if (postmaster) {
        return kPostmasterLabel;
    }
    // An account stack, such as a material or a shader, is kept by the profile rather than the
    // character, and the page that lists them is its inventory as much as the character's is.
    const state::AccountState& account = model().draft->after;
    for (std::size_t i = 0; i < account.profileItemCount; ++i) {
        if (account.profileItems[i].definitionHash == hash) {
            return kInInventoryLabel;
        }
    }
    return kNotInInventoryLabel;
}

/**
 * @return True when the field just drawn was left with Enter, which submits the grant.
 * The number fields apply what is typed as it is typed. Dear ImGui does not support asking a number
 * field to report Enter instead: it asserts in a debug build, and in a release build the field
 * applied a typed value only when Enter was the way out of it, so a click on an action granted the
 * value from before the edit.
 */
[[nodiscard]] bool left_with_enter() noexcept {
    return ImGui::IsItemDeactivated()
           && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
               || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
}

/** @return True when the catalog item is a stack rather than a single instance. */
[[nodiscard]] bool stackable(const edit::CatalogItem& definition) noexcept {
    using state::build_data::items::details::InstancedDefinitionState;
    return definition.detail.instancedDefinitionState == InstancedDefinitionState::stackable;
}

/**
 * Draws the pane's own header row, above the item: where the item is on the left, with the
 * action that moves it, and the pane's close on the right.
 * These are the editor's controls, not the game's, so they sit outside the item frame.
 * @param definition Catalog definition of the item, which says whether a slot takes it at all.
 * @param owned Owned instance, or null for a catalog item nobody owns yet.
 */
void draw_pane_header(const edit::CatalogItem& definition, const edit::Item* owned) noexcept {
    Model& state = model();
    const Placement placement = owned != nullptr ? placement_of(owned->instanceSoid) : Placement{};
    const char* where = owned == nullptr     ? holding_of(state.selection.definitionHash)
                        : placement.equipped ? kEquippedLabel
                        : owned->postmaster  ? kPostmasterLabel
                                             : kInInventoryLabel;
    // The word is set as the game sets its status words, in spaced capitals, level with the words
    // on the buttons beside it. The run leaves the cursor where it was, so a dummy holds its place.
    ImGui::AlignTextToFramePadding();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float wordWidth = controls::spaced(where,
                                             {at.x, at.y + ImGui::GetStyle().FramePadding.y},
                                             ImGui::GetColorU32(tooltip::muted()));
    ImGui::Dummy({wordWidth, ImGui::GetTextLineHeight()});
    if (placement.equipped) {
        ImGui::SameLine();
        if (ImGui::SmallButton(kUnequipLabel)) {
            record_edit(edit::unequip(
                *state.draft, state.catalog, state.character, placement.slot, state.status));
        }
    } else if (owned != nullptr && owned->postmaster) {
        // A postmaster item cannot be equipped until it is pulled into its own bucket.
        ImGui::SameLine();
        if (ImGui::SmallButton(kPullLabel)) {
            record_edit(edit::pull_from_postmaster(
                *state.draft, state.catalog, state.character, owned->instanceSoid, state.status));
        }
    } else if (owned != nullptr && definition.slot < inv::kEquipmentSlotCount) {
        // A stowed item goes on from here as it does from its card, so the pane is never the one
        // place an item can be looked at but not worn.
        ImGui::SameLine();
        if (ImGui::SmallButton(kEquipLabel)) {
            record_edit(edit::equip(
                *state.draft, state.catalog, state.character, owned->instanceSoid, state.status));
        }
    }
    const float closeWidth =
        ImGui::CalcTextSize(kCloseLabel).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - closeWidth);
    if (ImGui::SmallButton(kCloseLabel)) {
        clear_selection();
    }
}

/** @return True when the game gives this item a Power, which only gear carries. */
[[nodiscard]] bool carries_power(const edit::CatalogItem& definition) noexcept {
    return definition.kind == edit::GearKind::weapon || definition.kind == edit::GearKind::armor;
}

/**
 * Draws the grant controls for an item the character does not own yet: a power field for gear,
 * a quantity field for a stack, then the add actions on one row. Enter in either field adds.
 */
void draw_grant(const edit::CatalogItem& definition) noexcept {
    Model& state = model();
    const bool powered = carries_power(definition);
    const bool stacked = stackable(definition);
    const bool equippable = definition.slot < inv::kEquipmentSlotCount;
    bool add = false;
    bool equip = false;
    // Both fields share one label column, and only the fields the item has are offered: an
    // emblem has no Power and a weapon is no stack.
    const float column = ImGui::CalcTextSize(powered ? "Power" : "Quantity").x;
    if (powered) {
        controls::field_label("Power", column);
        ImGui::SetNextItemWidth(pixels(kGrantFieldWidth));
        (void)ImGui::InputInt("##power", &state.grant.power, 0, 0);
        add |= left_with_enter();
        state.grant.power = std::clamp(state.grant.power, 0, kPowerSliderMaximum);
    }
    if (stacked) {
        controls::field_label("Quantity", column);
        ImGui::SetNextItemWidth(pixels(kGrantFieldWidth));
        (void)ImGui::InputInt("##quantity", &state.grant.quantity, 0, 0);
        add |= left_with_enter();
        state.grant.quantity =
            std::clamp(state.grant.quantity, 1, (std::max)(1, definition.detail.maxStackSize));
    }
    if (powered || stacked) {
        controls::space(controls::kRowSpacing);
    }

    // The two actions share one row, the primary first; an item nothing equips gets one action.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = ImGui::GetContentRegionAvail().x;
    const float half = equippable ? (width - spacing) * 0.5F : width;
    add |= controls::primary_button("Add to Inventory", {half, pixels(controls::kActionHeight)});
    if (equippable) {
        ImGui::SameLine();
        equip = ImGui::Button("Add and Equip", {half, pixels(controls::kActionHeight)});
    }
    if (!add && !equip) {
        return;
    }
    const bool granted = edit::give(*state.draft,
                                    state.catalog,
                                    state.character,
                                    definition.definition.definitionHash,
                                    stacked ? state.grant.quantity : 1,
                                    level_of(state.grant.power),
                                    equip,
                                    state.status);
    record_edit(granted);
    // The owned-item editor takes over on the next frame, bound to the instance just equipped.
    if (equip && granted && equippable) {
        const auto& slot = character().equipment.slots[definition.slot];
        if (slot) {
            state.selection.instanceSoid = slot->instanceSoid;
        }
    }
}

/**
 * Draws the removal action. An item that cannot be removed yet keeps it, disabled, with the reason
 * under the pointer, as the card's menu does: an action that vanished never said what would bring
 * it back.
 * @param refusal Why the item cannot be removed yet, or null when it can.
 */
void draw_remove_action(const char* refusal) noexcept {
    ImGui::BeginDisabled(refusal != nullptr);
    const bool pressed =
        ImGui::Button("Remove from Inventory", {-FLT_MIN, pixels(controls::kActionHeight)});
    ImGui::EndDisabled();
    if (refusal != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", refusal);
    }
    if (pressed) {
        request_removal(model().selection.instanceSoid);
    }
}

/** @return How many sockets one owned item resolves to, or zero when it has none. */
[[nodiscard]] std::size_t socket_count(const edit::Item& item) noexcept {
    edit::Item resolved = item;
    return edit::materialize(resolved, model().catalog) ? resolved.sockets.plugCount : 0;
}

/**
 * Draws the item itself: the tooltip's own summary, with the armor stat block taking targets,
 * then every socket as a row that opens its picker. All of it sits in one tooltip frame, so the
 * item reads in the pane exactly as it does under the pointer, with the editing laid into it.
 * @param definition Catalog definition of the item.
 * @param owned Owned instance, or null for a catalog item nobody owns yet.
 * @param width Outer width of the frame in framebuffer pixels.
 */
void draw_item_frame(const edit::CatalogItem& definition,
                     edit::Item* owned,
                     float width) noexcept {
    // The bars only take a drag when a roll can actually move: armor whose stat plugs have no
    // alternative in their socket is shown as the tooltip shows it, and nothing pretends otherwise.
    const bool armor = owned != nullptr && definition.kind == edit::GearKind::armor
                       && edit::adjustable_stats(*owned, model().catalog);
    const std::size_t sockets = owned != nullptr ? socket_count(*owned) : 0;
    if (!tooltip::begin_frame("item", width)) {
        tooltip::end_frame();
        return;
    }
    const float inner = width - (tooltip::padding() * 2.0F);
    tooltip::StatEdit stats;
    if (armor) {
        stats.targets = &stat_targets_for(*owned);
        stats.limit = kMaximumStatTarget;
    }
    tooltip::draw_summary(definition, owned, inner, armor || sockets != 0, armor ? &stats : nullptr);
    if (armor) {
        settle_stat_targets(*owned, stats.released);
    }
    if (sockets != 0) {
        // The sockets meet the rule over them, so the intrinsic's panel runs up to it.
        tooltip::draw_row_rule();
        draw_perk_editor(definition, *owned, inner);
    }
    tooltip::end_frame();
}

/**
 * Draws the row that sends a stowed item to another character, under a label naming it, with a
 * button for each, whatever its class.
 * @return True when the item was sent, which takes it off this character: the caller must not
 * read the item again.
 */
[[nodiscard]] bool draw_send_row(const edit::Item& item) noexcept {
    const Model& state = model();
    const state::AccountState& account = state.draft->after;
    if (account.characterCount < 2) {
        return false;
    }
    controls::space(controls::kSectionSpacing);
    tooltip::draw_label(kSendLabel);
    // The characters share the row evenly at the pane's action height, as the grant's actions do.
    const float others = static_cast<float>(account.characterCount - 1);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 size{(ImGui::GetContentRegionAvail().x - (spacing * (others - 1.0F))) / others,
                      pixels(controls::kActionHeight)};
    bool first = true;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (index == state.character) {
            continue;
        }
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        ImGui::PushID(static_cast<int>(index));
        const bool pressed = ImGui::Button(character_label(index).c_str(), size);
        ImGui::PopID();
        if (pressed && send_item(item.instanceSoid, index)) {
            return true;
        }
    }
    return false;
}

/** Draws what the pane offers under the item frame for an item the character owns. */
void draw_owned_actions(const edit::CatalogItem& definition, edit::Item& item) noexcept {
    if (stackable(definition)) {
        controls::space(controls::kSectionSpacing);
        const float column = ImGui::CalcTextSize("Quantity").x;
        controls::field_label("Quantity", column);
        // The grant's own quantity field: typed, at the same width, with no steppers.
        ImGui::SetNextItemWidth(pixels(kGrantFieldWidth));
        const bool quantityChanged = ImGui::InputInt("##owned_quantity", &item.quantity, 0, 0);
        if (quantityChanged) {
            item.quantity =
                std::clamp(item.quantity, 1, (std::max)(1, definition.detail.maxStackSize));
        }
        record_scalar_edit(quantityChanged);
    }
    const bool stowed = !placement_of(item.instanceSoid).equipped;
    // A postmaster item is pulled before it goes anywhere, as in game.
    if (stowed && !item.postmaster && draw_send_row(item)) {
        return;
    }
    const bool locked = (item.flags & inv::kLockedItemFlag) != 0;
    controls::space(controls::kSectionSpacing);
    draw_remove_action(!stowed ? kUnequipToRemove : locked ? kUnlockToRemove : nullptr);
}

} // namespace

void request_removal(std::uint64_t instance) noexcept {
    Model& state = model();
    state.removal = instance;
    // The confirmation is opened from the page, as the perk picker is, so the card or the pane
    // that asked can scroll away or close without taking the question with it.
    state.removalRequested = true;
}

void draw_removal_confirm() noexcept {
    Model& state = model();
    if (state.removalRequested) {
        state.removalRequested = false;
        ImGui::OpenPopup(kRemoveItemTitle);
    }
    if (!ImGui::BeginPopupModal(kRemoveItemTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const edit::Item* item = find_owned_item(state.removal);
    if (item == nullptr) {
        // The item went while the question was open, to another edit or the account reloading.
        state.removal = 0;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    // The item is named at the pane's subheading size, and one muted line says whose inventory it
    // leaves and how to get it back, which is all the player needs to answer.
    const edit::CatalogItem* definition = state.catalog.find(item->definitionHash);
    controls::title(definition != nullptr ? definition->name.c_str() : "This Item",
                    controls::kSubheadingScale);
    const std::string owner = character_label(state.character);
    ImGui::TextColored(tooltip::muted(),
                       "Removes it from %s's inventory. Undo brings it back.",
                       owner.c_str());
    controls::space(controls::kRowSpacing);
    const controls::Answer answer = controls::confirm_footer("Remove");
    if (answer == controls::Answer::confirm) {
        (void)erase_owned_item(state.removal);
    }
    if (answer != controls::Answer::none) {
        state.removal = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_inspector() noexcept {
    Model& state = model();
    const edit::CatalogItem* definition = state.catalog.find(state.selection.definitionHash);
    if (definition == nullptr) {
        ImGui::TextDisabled("No item selected.");
        return;
    }
    draw_pane_header(*definition, selected_item());
    // Equipping and unequipping move the item between two arrays, so the pointer is taken again.
    edit::Item* owned = selected_item();
    controls::space(controls::kRowSpacing);
    draw_item_frame(*definition, owned, ImGui::GetContentRegionAvail().x);
    if (owned != nullptr) {
        draw_owned_actions(*definition, *owned);
    } else {
        controls::space(controls::kSectionSpacing);
        draw_grant(*definition);
    }
}

} // namespace dawn::core::ui::modules::loadout::internal
