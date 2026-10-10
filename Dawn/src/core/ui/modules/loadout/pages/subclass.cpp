// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <imgui.h>

#include "../../../scaling/dpi/ui_dpi_scaling.h"
#include "../controls.h"
#include "../internal.h"

namespace dawn::core::ui::modules::loadout::internal {
namespace {

using scaling::dpi::pixels;

/** One editable ability lane, paired with the character field that names its entry. */
struct AbilityLane {
    const char* label;
    std::uint8_t state::CharacterState::*field;
};

/**
 * Ability lanes in the order the catalog stores their option lists.
 * The catalog's five lists are jump, grenade, super, melee and class ability, in that order, so
 * the lane index also indexes `CatalogItem::abilities`.
 */
constexpr AbilityLane kAbilityLanes[]{
    {"Jump", &state::CharacterState::movementAbilityEntry},
    {"Grenade", &state::CharacterState::grenadeAbilityEntry},
    {"Super", &state::CharacterState::superAbilityEntry},
    {"Melee", &state::CharacterState::meleeAbilityEntry},
    {"Class Ability", &state::CharacterState::classAbilityEntry},
};
static_assert(std::size(kAbilityLanes)
                  == std::tuple_size_v<decltype(edit::CatalogItem::abilities)>,
              "Every catalog ability list needs an editable lane.");
/** Gap between the lines of an attunement's perk list. */
constexpr float kPerkListGap = 1.0F;
/** The two lanes the attunement sets as a pair. */
constexpr std::size_t kSuperLane = 2;
constexpr std::size_t kMeleeLane = 3;

/** @return The subclass definition the character has equipped, or null when it has none. */
[[nodiscard]] const edit::CatalogItem* equipped_subclass() noexcept {
    const auto& slot = character().equipment.slots[kSubclassSlot];
    return slot ? model().catalog.find(slot->definitionHash) : nullptr;
}

/** @return True when one catalog item is a subclass Dawn can read the abilities of, of any class. */
[[nodiscard]] bool selectable_subclass(const edit::CatalogItem& item) noexcept {
    return item.kind == edit::GearKind::subclass && !item.abilities[0].empty();
}

/**
 * Moves each pick the subclass now on does not offer to its first choice, and the super and melee to
 * its first attunement when they are not one of its pairs. Picks carried over from another subclass,
 * a custom one above all, can name entries it lacks, and the apply could not resolve them.
 */
void fit_picks(const edit::CatalogItem& definition) noexcept {
    state::CharacterState& owner = character();
    bool moved = false;
    for (std::size_t lane = 0; lane < std::size(kAbilityLanes); ++lane) {
        if (lane == kSuperLane || lane == kMeleeLane) {
            continue;
        }
        const auto& options = definition.abilities[lane];
        std::uint8_t& entry = owner.*kAbilityLanes[lane].field;
        if (!options.empty() && std::none_of(options.begin(), options.end(), [&](const auto& choice) { return choice.entry == entry; })) {
            entry = options.front().entry;
            moved = true;
        }
    }
    const bool paired = std::any_of(definition.paths.begin(), definition.paths.end(), [&](const edit::SubclassPath& path) {
        return path.super == owner.superAbilityEntry && path.melee == owner.meleeAbilityEntry;
    });
    if (!paired && !definition.paths.empty()) {
        owner.superAbilityEntry = definition.paths.front().super;
        owner.meleeAbilityEntry = definition.paths.front().melee;
        moved = true;
    }
    if (moved) {
        mark_changed();
    }
}

/** Equips one subclass, reusing the owned instance when the character already has it. */
void choose_subclass(const edit::CatalogItem& definition) noexcept {
    Model& state = model();
    state::CharacterState& owner = character();
    std::uint64_t owned = 0;
    for (std::size_t i = 0; i < owner.inventory.count; ++i) {
        if (owner.inventory.values[i].definitionHash == definition.definition.definitionHash) {
            owned = owner.inventory.values[i].instanceSoid;
            break;
        }
    }
    record_edit(owned != 0
                    ? edit::equip(*state.draft, state.catalog, state.character, owned, state.status)
                    : edit::give(*state.draft,
                                 state.catalog,
                                 state.character,
                                 definition.definition.definitionHash,
                                 1,
                                 level_of(state.grant.power),
                                 true,
                                 state.status));
    if (const edit::CatalogItem* now = equipped_subclass(); now == &definition) {
        fit_picks(definition);
    }
}

/**
 * Draws the attunement picker, which is what the game lets a player choose: the super and the
 * melee come as a pair with it, so neither is offered on its own. The attunement's perks are
 * listed under the picker, as Sundial lists them, so the choice reads as what it gives.
 */
void draw_attunement_field(const edit::CatalogItem& definition,
                           float labelWidth,
                           float controlWidth) noexcept {
    state::CharacterState& owner = character();
    const edit::SubclassPath* current = nullptr;
    for (const auto& path : definition.paths) {
        if (path.super == owner.superAbilityEntry && path.melee == owner.meleeAbilityEntry) {
            current = &path;
        }
    }
    controls::field_label("Attunement", labelWidth);
    if (controls::begin_picker(
            "##attunement", current != nullptr ? current->name.c_str() : "Custom", controlWidth)) {
        for (const auto& path : definition.paths) {
            if (controls::picker_row(path.name.c_str(), &path == current)) {
                owner.superAbilityEntry = path.super;
                owner.meleeAbilityEntry = path.melee;
                mark_changed();
            }
        }
        controls::end_picker();
    }
    if (current == nullptr) {
        return;
    }
    // The perks are a list under the field, set tight so they read as one note, not three rows.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2{ImGui::GetStyle().ItemSpacing.x, pixels(kPerkListGap)});
    for (const std::string& perk : current->perks) {
        controls::field_label("", labelWidth);
        ImGui::TextDisabled("%s", perk.c_str());
    }
    ImGui::PopStyleVar();
}

/** Draws one ability picker over the character field that names its selected entry. */
void draw_ability_field(const edit::CatalogItem& definition,
                        std::size_t lane,
                        float labelWidth,
                        float controlWidth) noexcept {
    state::CharacterState& owner = character();
    std::uint8_t& entry = owner.*kAbilityLanes[lane].field;
    const auto& options = definition.abilities[lane];
    const char* active = "None";
    for (const auto& choice : options) {
        if (choice.entry == entry) {
            active = choice.name.c_str();
        }
    }
    ImGui::PushID(static_cast<int>(lane));
    controls::field_label(kAbilityLanes[lane].label, labelWidth);
    if (controls::begin_picker("##ability", active, controlWidth)) {
        for (const auto& choice : options) {
            ImGui::PushID(choice.entry);
            if (controls::picker_row(choice.name.c_str(), choice.entry == entry)) {
                entry = choice.entry;
                mark_changed();
            }
            ImGui::PopID();
        }
        controls::end_picker();
    }
    ImGui::PopID();
}

} // namespace

void draw_subclass_group(float labelWidth, float controlWidth) noexcept {
    Model& state = model();
    const edit::CatalogItem* current = equipped_subclass();
    const bool equipped = character().equipment.slots[kSubclassSlot].has_value();
    controls::field_label("Subclass", labelWidth);
    if (controls::begin_picker("##subclass",
                               current != nullptr ? current->name.c_str()
                               : equipped         ? "Not in This Build"
                                                  : "None",
                               controlWidth)) {
        for (const auto& definition : state.catalog.items) {
            if (!selectable_subclass(definition)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(definition.definition.definitionIndex));
            if (controls::picker_row(definition.name.c_str(), &definition == current)) {
                choose_subclass(definition);
            }
            ImGui::PopID();
        }
        controls::end_picker();
    }
    if (current != nullptr && !current->paths.empty()) {
        draw_attunement_field(*current, labelWidth, controlWidth);
    }
}

void draw_ability_group(float labelWidth, float controlWidth) noexcept {
    const edit::CatalogItem* definition = equipped_subclass();
    if (!character().equipment.slots[kSubclassSlot]) {
        ImGui::TextDisabled("No subclass equipped.");
        return;
    }
    if (definition == nullptr) {
        ImGui::TextDisabled("The equipped subclass is not in the installed build.");
        return;
    }
    if (definition->abilities[0].empty()) {
        ImGui::TextDisabled("Dawn could not read this subclass's abilities.");
        return;
    }
    // The super and the melee lanes follow the attunement, so only the lanes the game lets a
    // player set on their own are offered here.
    for (std::size_t lane = 0; lane < std::size(kAbilityLanes); ++lane) {
        if (lane == kSuperLane || lane == kMeleeLane) {
            continue;
        }
        draw_ability_field(*definition, lane, labelWidth, controlWidth);
    }
}

} // namespace dawn::core::ui::modules::loadout::internal
