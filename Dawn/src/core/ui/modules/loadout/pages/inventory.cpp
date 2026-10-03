// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../../scaling/dpi/ui_dpi_scaling.h"
#include "../card.h"
#include "../internal.h"
#include "../controls.h"
#include "../art.h"
#include "../tooltip.h"

namespace dawn::core::ui::modules::loadout::internal {
namespace {

using scaling::dpi::pixels;
namespace inv = state::account::inventory;

/** Width of the search box each inventory page puts at the head of its filter row. */
constexpr float kSearchWidth = 220.0F;
/**
 * A row of the page's two lists, the profile's stacks and the search's other results alike: the
 * icon's edge, the inset at either end, and the room the row keeps over and under its content.
 */
constexpr float kProfileIconExtent = 28.0F;
constexpr float kProfileRowInset = 6.0F;
constexpr float kProfileRowPadding = 2.0F;
/** Narrowest a list row is drawn, which sets the column count. */
constexpr float kProfileRowMinimumWidth = 320.0F;
/** 64 authored pixels hold a five-figure stack without the field reading as an empty box. */
constexpr float kQuantityWidth = 64.0F;
/** The add action on each page's header row. */
constexpr const char* kAddItemLabel = "Add Item";
/** The removal says what it does. A glyph on its own read as decoration rather than a control. */
constexpr const char* kRemoveLabel = "Remove";
/** Section a stack falls under when its type is blank, or when the catalog does not carry it. */
constexpr const char* kUntypedGroup = "Other";
constexpr const char* kUnknownGroup = "Unknown";
/** Largest stack the editor offers when the catalog does not name a limit. */
constexpr int kUnknownStackLimit = 9999;
/** Title of the stack removal, used for both the action and its modal. */
constexpr const char* kRemoveStackTitle = "Remove Stack?";
/** Id of a stack row's own menu, which offers the removal the row's Remove does. */
constexpr const char* kStackMenuId = "stack_menu";
/** What every removal on the page says under what goes: it is an edit like any other. */
constexpr const char* kUndoOneNote = "Undo brings it back.";
constexpr const char* kUndoManyNote = "Undo brings them back.";
/** What a list says when a search or a filter leaves nothing in it. */
constexpr const char* kNoMatchLabel = "No items match.";
/**
 * The bar that stands over the grid while anything is selected: a strip in the choice fill with the
 * accent's rail, holding the count and what can be done to all of them at once.
 */
constexpr float kPickBarPadding = 4.0F;
constexpr float kPickBarInset = 10.0F;
/**
 * Id of the removal of every selected item, whose title says whether it removes one item or
 * several. Everything after ### names the popup, so the title can change in front of it.
 */
constexpr const char* kRemovePickedId = "###remove_selected";
/** The bar's actions, which are measured before they are drawn. */
constexpr const char* kLockLabel = "Lock";
constexpr const char* kUnlockLabel = "Unlock";
constexpr const char* kSendLabel = "Send To";
constexpr const char* kSetPowerLabel = "Set Power";
constexpr const char* kPullLabel = "Pull";
constexpr const char* kSelectAllLabel = "Select All Shown";
constexpr const char* kClearLabel = "Clear Selection";
/** Why an action on the selection is disabled, which it says under the pointer. */
constexpr const char* kPowerlessTip = "No selected item carries Power.";
constexpr const char* kNotWaitingTip = "No selected item is at the Postmaster.";
constexpr const char* kKeptTip = "Locked and equipped items can't be removed.";
/**
 * The count keeps a slot as wide as three figures, more than a character carries, so the actions
 * after it hold still as the count grows.
 */
constexpr const char* kPickCountSample = "000 Selected";
/** 64 authored pixels hold a five-figure Power in the selection bar's field. */
constexpr float kPickPowerWidth = 64.0F;
/** 192 bytes hold any outcome the bar reports, with every count it can give. */
constexpr std::size_t kMessageCapacity = 192;
/** The search's other results: the section's title, and the name its profile stacks go under. */
constexpr const char* kElsewhereLabel = "Elsewhere on the Account";
constexpr const char* kProfileLabel = "Profile";
/** Where a result elsewhere is kept, after the name of whoever keeps it. */
constexpr const char* kEquippedWhere = "Equipped";
constexpr const char* kPostmasterWhere = "Postmaster";
/** A result that belongs to the account rather than a character. */
constexpr std::size_t kAccountItems = static_cast<std::size_t>(-1);

/** @return The width a button takes for its label, as Dear ImGui sizes one: the words and the frame padding. */
[[nodiscard]] float button_width(const char* label) noexcept {
    return ImGui::CalcTextSize(label, nullptr, true).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
}

/**
 * Draws the page's add action at the far end of its header row, which opens the armory on what the
 * page is made of. The armory is where an item is granted from; the page only has to say where to go.
 * A page narrowed to one slot opens the armory on that slot's category, narrowed the same way, as a
 * card's swap does. Any other page opens it on the page's own category across every slot, so the
 * armory never keeps a slot its category has no items for.
 * @param category Armory category the page opens on when it is not narrowed to a slot.
 * @param slot Equipment slot the page is narrowed to, or -1.
 */
void draw_add_action(Category category, int slot) noexcept {
    Model& state = model();
    // The side workspace lies over the page's right edge, so the action keeps to what it leaves.
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - overlay_width()
                    - button_width(kAddItemLabel));
    if (!ImGui::Button(kAddItemLabel)) {
        return;
    }
    state.view = View::armory;
    state.browse.category = category;
    state.browse.slot = -1;
    // The armory carries no subclasses, so a page narrowed to the subclass opens on its own category.
    if (slot >= 0 && static_cast<std::size_t>(slot) != kSubclassSlot) {
        const auto index = static_cast<std::size_t>(slot);
        state.browse.category = index <= kLastWeaponSlot  ? Category::weapons
                                : index <= kLastArmorSlot ? Category::armor
                                                          : Category::cosmetics;
        state.browse.slot = slot;
    }
    state.browse.type.clear();
    state.results.key.clear();
}

/** Filter words and what each asks for, as a player types them after is: */
constexpr const char* kTierWords[]{"unclassified", "common", "uncommon", "rare", "legendary", "exotic"};

/** @return True when the game gives an item a Power, which only gear carries. */
[[nodiscard]] bool carries_power(const edit::CatalogItem& definition) noexcept {
    return definition.kind == edit::GearKind::weapon || definition.kind == edit::GearKind::armor;
}

} // namespace

Query read_query(const std::string& search) noexcept {
    Query query;
    const auto keep = [&query](const std::string& word) {
        query.words += query.words.empty() ? word : " " + word;
    };
    std::size_t begin = 0;
    while (begin < search.size()) {
        const std::size_t end = search.find(' ', begin);
        const std::string word = search.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        begin = end == std::string::npos ? search.size() : end + 1;
        if (word.empty()) {
            continue;
        }
        if (word.rfind("is:", 0) == 0) {
            const std::string what = word.substr(3);
            bool known = true;
            const auto tier = std::find_if(std::begin(kTierWords), std::end(kTierWords), [&what](const char* name) {
                return what == name;
            });
            if (tier != std::end(kTierWords)) {
                query.tier = static_cast<int>(tier - std::begin(kTierWords));
            } else if (what == "weapon" || what == "weapons") {
                query.kind = edit::GearKind::weapon;
                query.kinded = true;
            } else if (what == "armor") {
                query.kind = edit::GearKind::armor;
                query.kinded = true;
            } else if (what == "arc" || what == "solar" || what == "void") {
                query.damageType = what == "arc" ? edit::DamageType::arc : what == "solar" ? edit::DamageType::solar : edit::DamageType::void_;
                query.damageTyped = true;
            } else if (what == "locked" || what == "unlocked") {
                query.locked = what == "locked" ? 1 : 0;
            } else if (what == "equipped") {
                query.equipped = 1;
            } else if (what == "postmaster") {
                query.postmaster = 1;
            } else {
                known = false;
            }
            if (!known) {
                keep(word);
            }
            continue;
        }
        if (word.rfind("power:", 0) == 0) {
            std::string_view rest = std::string_view(word).substr(6);
            char comparison = '=';
            if (rest.rfind(">=", 0) == 0 || rest.rfind("<=", 0) == 0) {
                comparison = rest[0] == '>' ? 'g' : 'l';
                rest.remove_prefix(2);
            } else if (!rest.empty() && (rest[0] == '>' || rest[0] == '<' || rest[0] == '=')) {
                comparison = rest[0];
                rest.remove_prefix(1);
            }
            int power = 0;
            bool digits = !rest.empty() && rest.size() <= 6;
            for (const char c : rest) {
                digits = digits && c >= '0' && c <= '9';
                power = digits ? (power * 10) + (c - '0') : power;
            }
            if (digits) {
                query.comparison = comparison;
                query.power = power;
            } else {
                keep(word);
            }
            continue;
        }
        keep(word);
    }
    return query;
}

bool passes(const Query& query, const edit::CatalogItem& definition, const edit::Item* item, bool equipped) noexcept {
    if ((!query.words.empty() && !edit::matches(definition, query.words))
        || (query.tier >= 0 && definition.definition.tier != query.tier)
        || (query.kinded && definition.kind != query.kind)
        || (query.damageTyped && edit::item_damage_type(definition, item, model().catalog) != query.damageType)) {
        return false;
    }
    if (item == nullptr) {
        return !query.held_only();
    }
    if ((query.locked >= 0 && ((item->flags & inv::kLockedItemFlag) != 0) != (query.locked == 1))
        || (query.equipped >= 0 && equipped != (query.equipped == 1))
        || (query.postmaster >= 0 && item->postmaster != (query.postmaster == 1))) {
        return false;
    }
    if (query.comparison == 0) {
        return true;
    }
    if (!carries_power(definition)) {
        return false;
    }
    const int power = power_of(item->level);
    switch (query.comparison) {
    case '<':
        return power < query.power;
    case '>':
        return power > query.power;
    case 'l':
        return power <= query.power;
    case 'g':
        return power >= query.power;
    default:
        return power == query.power;
    }
}

namespace {

/** @return True when one owned item passes the page's search and slot filters. */
[[nodiscard]] bool passes_filters(const edit::Item& item, const Query& query, bool equipped) noexcept {
    Model& state = model();
    const edit::CatalogItem* definition = state.catalog.find(item.definitionHash);
    if (definition == nullptr || !passes(query, *definition, &item, equipped)) {
        return false;
    }
    return state.inventorySlot < 0
           || definition->slot == static_cast<std::size_t>(state.inventorySlot);
}

/**
 * @return Every character item matching the page filters, equipped gear first, then by type and
 * name. The equipped pieces belong in their buckets too: the page is the whole of what the
 * character carries, not only what is stowed.
 */
[[nodiscard]] std::vector<const edit::Item*> filtered_character_items() noexcept {
    Model& state = model();
    const Query query = read_query(edit::searchable(state.inventorySearch));
    const state::CharacterState& owner = character();
    std::vector<const edit::Item*> items;
    for (const auto& slot : owner.equipment.slots) {
        if (slot && passes_filters(*slot, query, true)) {
            items.push_back(&*slot);
        }
    }
    const std::size_t equippedCount = items.size();
    for (std::size_t i = 0; i < owner.inventory.count; ++i) {
        if (passes_filters(owner.inventory.values[i], query, false)) {
            items.push_back(&owner.inventory.values[i]);
        }
    }
    // Only items passes_filters already resolved reach this list, so a missing definition sorts
    // last rather than being dereferenced on the strength of that invariant holding elsewhere.
    const auto byTypeThenName = [&state](const edit::Item* a, const edit::Item* b) {
        const edit::CatalogItem* first = state.catalog.find(a->definitionHash);
        const edit::CatalogItem* second = state.catalog.find(b->definitionHash);
        if (first == nullptr || second == nullptr) {
            return first != nullptr;
        }
        return first->type == second->type ? first->name < second->name : first->type < second->type;
    };
    std::sort(items.begin() + static_cast<std::ptrdiff_t>(equippedCount), items.end(), byTypeThenName);
    return items;
}

/** @return True when the instance is sitting in one of the character's equipment slots. */
[[nodiscard]] bool is_equipped(std::uint64_t instance) noexcept {
    for (const auto& slot : character().equipment.slots) {
        if (slot && slot->instanceSoid == instance) {
            return true;
        }
    }
    return false;
}

/** @return How many equipment slots the character has filled. */
[[nodiscard]] std::size_t equipped_count() noexcept {
    std::size_t count = 0;
    for (const auto& slot : character().equipment.slots) {
        count += slot ? 1 : 0;
    }
    return count;
}

/**
 * @return True when a removal would take the item: the character holds it, and it is neither locked,
 * as the game keeps a locked item, nor equipped, which stays until something replaces it.
 */
[[nodiscard]] bool can_remove(std::uint64_t instance) noexcept {
    const edit::Item* item = find_owned_item(instance);
    return item != nullptr && !is_equipped(instance) && (item->flags & inv::kLockedItemFlag) == 0;
}

/** Lets go of every selected item the character no longer holds, such as one just sent away. */
void prune_picks() noexcept {
    std::vector<std::uint64_t>& picked = model().picked;
    picked.erase(std::remove_if(picked.begin(),
                                picked.end(),
                                [](std::uint64_t instance) { return find_owned_item(instance) == nullptr; }),
                 picked.end());
}

/** Says what an action on the selected items came to, and carries it into the apply that follows. */
void report_bulk(const char* message, bool changed) noexcept {
    Model& state = model();
    state.status = message;
    state.statusFailed = false;
    if (changed) {
        state.editNote = message;
        mark_changed();
    }
}

/** Locks, or unlocks, every selected item that is not so already. */
void lock_picked(bool lock) noexcept {
    std::size_t changed = 0;
    for (const std::uint64_t instance : model().picked) {
        edit::Item* item = find_owned_item(instance);
        if (item == nullptr || ((item->flags & inv::kLockedItemFlag) != 0) == lock) {
            continue;
        }
        item->flags = lock ? item->flags | inv::kLockedItemFlag : item->flags & ~inv::kLockedItemFlag;
        ++changed;
    }
    char message[kMessageCapacity]{};
    if (changed == 0) {
        (void)std::snprintf(message, sizeof message, "All selected items are already %s.", lock ? "locked" : "unlocked");
    } else {
        (void)std::snprintf(message,
                            sizeof message,
                            "%s %zu %s.",
                            lock ? "Locked" : "Unlocked",
                            changed,
                            changed == 1 ? "item" : "items");
    }
    report_bulk(message, changed != 0);
}

/**
 * Sends every selected item that can go to another character. One that is equipped, at the
 * postmaster or of a class the other character cannot hold stays, and stays selected, so the page
 * shows what did not go.
 */
void send_picked(std::size_t target) noexcept {
    Model& state = model();
    const state::CharacterClass targetClass = state.draft->after.characters[target].characterClass;
    std::size_t sent = 0;
    std::size_t equipped = 0;
    std::size_t postmaster = 0;
    std::size_t otherClass = 0;
    std::size_t noRoom = 0;
    std::vector<std::uint64_t> stayed;
    const std::vector<std::uint64_t> picked = state.picked;
    for (const std::uint64_t instance : picked) {
        const edit::Item* item = find_owned_item(instance);
        const edit::CatalogItem* definition = item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
        if (item == nullptr || definition == nullptr) {
            continue;
        }
        std::size_t* reason = is_equipped(instance) ? &equipped
                              : item->postmaster     ? &postmaster
                              : !edit::fits_class(*definition, targetClass) ? &otherClass
                                                                            : nullptr;
        std::string refused;
        if (reason == nullptr && !edit::transfer(*state.draft, state.catalog, state.character, target, instance, refused)) {
            reason = &noRoom;
        }
        if (reason != nullptr) {
            ++*reason;
            stayed.push_back(instance);
            continue;
        }
        ++sent;
        if (state.selection.instanceSoid == instance) {
            clear_selection();
        }
    }
    state.picked = stayed;
    const std::string name = character_label(target);
    char message[kMessageCapacity]{};
    int written = sent == 0 ? std::snprintf(message, sizeof message, "Nothing was sent to %s.", name.c_str())
                            : std::snprintf(message, sizeof message, "Sent %zu %s to %s.", sent, sent == 1 ? "item" : "items", name.c_str());
    const auto note = [&](std::size_t count, const char* why) {
        if (count != 0 && written >= 0 && static_cast<std::size_t>(written) < sizeof message) {
            written += std::snprintf(message + written, sizeof message - static_cast<std::size_t>(written), " %zu %s.", count, why);
        }
    };
    note(equipped, equipped == 1 ? "is equipped" : "are equipped");
    note(postmaster, postmaster == 1 ? "is at the Postmaster" : "are at the Postmaster");
    note(otherClass, otherClass == 1 ? "belongs to another class" : "belong to another class");
    note(noRoom, "did not fit there");
    report_bulk(message, sent != 0);
    model().statusFailed = sent == 0;
}

/** Sets every selected weapon and armor piece to one Power. What carries no Power is left as it is. */
void set_power_picked(int power) noexcept {
    Model& state = model();
    const int level = std::clamp(level_of(power), 0, kMaximumItemLevel);
    std::size_t changed = 0;
    std::size_t powerless = 0;
    for (const std::uint64_t instance : state.picked) {
        edit::Item* item = find_owned_item(instance);
        const edit::CatalogItem* definition = item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
        if (definition == nullptr || !carries_power(*definition)) {
            ++powerless;
            continue;
        }
        if (item->level != level) {
            item->level = level;
            ++changed;
        }
    }
    char message[kMessageCapacity]{};
    int written = changed == 0
                      ? std::snprintf(message, sizeof message, "Already at %d Power.", power_of(level))
                      : std::snprintf(message,
                                      sizeof message,
                                      "Set %zu %s to %d Power.",
                                      changed,
                                      changed == 1 ? "piece" : "pieces",
                                      power_of(level));
    if (powerless != 0 && written >= 0 && static_cast<std::size_t>(written) < sizeof message) {
        (void)std::snprintf(message + written,
                            sizeof message - static_cast<std::size_t>(written),
                            " %zu %s no Power.",
                            powerless,
                            powerless == 1 ? "item carries" : "items carry");
    }
    report_bulk(message, changed != 0);
}

/**
 * Pulls every selected item at the postmaster into its own bucket, as the postmaster does. One whose
 * bucket is full, or that belongs to the account, stays there.
 */
void pull_picked() noexcept {
    Model& state = model();
    std::size_t pulled = 0;
    std::size_t stuck = 0;
    const std::vector<std::uint64_t> picked = state.picked;
    for (const std::uint64_t instance : picked) {
        const edit::Item* item = find_owned_item(instance);
        if (item == nullptr || !item->postmaster) {
            continue;
        }
        std::string refused;
        if (edit::pull_from_postmaster(*state.draft, state.catalog, state.character, instance, refused)) {
            ++pulled;
        } else {
            ++stuck;
        }
    }
    char message[kMessageCapacity]{};
    int written = std::snprintf(message,
                                sizeof message,
                                "Pulled %zu %s from the Postmaster.",
                                pulled,
                                pulled == 1 ? "item" : "items");
    if (stuck != 0 && written >= 0 && static_cast<std::size_t>(written) < sizeof message) {
        (void)std::snprintf(message + written,
                            sizeof message - static_cast<std::size_t>(written),
                            " %zu stayed at the Postmaster.",
                            stuck);
    }
    report_bulk(message, pulled != 0);
    state.statusFailed = pulled == 0;
}

/**
 * Removes every selected item that can be removed. A locked item stays, as the game keeps a locked
 * item, and an equipped one stays until something replaces it; both stay selected.
 */
void remove_picked() noexcept {
    Model& state = model();
    std::size_t removed = 0;
    std::size_t locked = 0;
    std::size_t equipped = 0;
    std::vector<std::uint64_t> stayed;
    const std::vector<std::uint64_t> picked = state.picked;
    for (const std::uint64_t instance : picked) {
        const edit::Item* item = find_owned_item(instance);
        if (item == nullptr) {
            continue;
        }
        if (is_equipped(instance)) {
            ++equipped;
            stayed.push_back(instance);
        } else if ((item->flags & inv::kLockedItemFlag) != 0) {
            ++locked;
            stayed.push_back(instance);
        } else if (erase_owned_item(instance)) {
            ++removed;
        }
    }
    state.picked = stayed;
    char message[kMessageCapacity]{};
    int written = std::snprintf(message,
                                sizeof message,
                                "Removed %zu %s.",
                                removed,
                                removed == 1 ? "item" : "items");
    if (locked + equipped != 0 && written >= 0 && static_cast<std::size_t>(written) < sizeof message) {
        (void)std::snprintf(message + written,
                            sizeof message - static_cast<std::size_t>(written),
                            " %zu locked or equipped %s stayed.",
                            locked + equipped,
                            locked + equipped == 1 ? "item" : "items");
    }
    report_bulk(message, removed != 0);
}

/** An action on every selected item, carried out once the page has drawn the items it would move. */
struct Bulk {
    enum class Kind : std::uint8_t { none, lock, unlock, send, power, pull, remove };
    Kind kind{Kind::none};
    /** Character a send goes to. */
    std::size_t target{};
};

/** Carries out one action on every selected item. */
void run_bulk(const Bulk& bulk) noexcept {
    switch (bulk.kind) {
    case Bulk::Kind::none:
        break;
    case Bulk::Kind::lock:
        lock_picked(true);
        break;
    case Bulk::Kind::unlock:
        lock_picked(false);
        break;
    case Bulk::Kind::send:
        send_picked(bulk.target);
        break;
    case Bulk::Kind::power:
        set_power_picked(model().pickPower);
        break;
    case Bulk::Kind::pull:
        pull_picked();
        break;
    case Bulk::Kind::remove:
        remove_picked();
        break;
    }
}

/**
 * Draws the body every removal on the page is laid out in, inside its open modal: what goes, one muted
 * line that ends on what Undo does, and the confirmation's two buttons. A removal is an edit like any
 * other, so it can be taken back; saying so is what makes it safe. Either answer closes the modal.
 * @param what The item's name, or how many items go.
 * @param note The muted line.
 * @return True when the removal was confirmed.
 */
[[nodiscard]] bool confirm_removal(const char* what, const char* note) noexcept {
    ImGui::TextUnformatted(what);
    ImGui::TextColored(tooltip::muted(), "%s", note);
    controls::space(controls::kRowSpacing);
    const controls::Answer answer = controls::confirm_footer(kRemoveLabel);
    if (answer != controls::Answer::none) {
        ImGui::CloseCurrentPopup();
    }
    return answer == controls::Answer::confirm;
}

/**
 * Draws the confirmation the bar's Remove opens, as every removal on the page reads: the item's name,
 * or how many go when several do, then what stays and that Undo brings them back.
 * @return True when the removal was confirmed.
 */
[[nodiscard]] bool draw_remove_picked_modal() noexcept {
    Model& state = model();
    if (!ImGui::IsPopupOpen(kRemovePickedId)) {
        return false;
    }
    const edit::CatalogItem* definition = nullptr;
    std::size_t going = 0;
    std::size_t kept = 0;
    for (const std::uint64_t instance : state.picked) {
        const edit::Item* item = find_owned_item(instance);
        if (item == nullptr) {
            continue;
        }
        if (!can_remove(instance)) {
            ++kept;
            continue;
        }
        definition = state.catalog.find(item->definitionHash);
        ++going;
    }
    // One item reads as the pane's own removal does; several are counted. The id after ### keeps the
    // popup the same one whichever title it shows.
    const bool one = going == 1;
    char title[kMessageCapacity]{};
    (void)std::snprintf(title, sizeof title, "%s%s", one ? "Remove Item?" : "Remove Items?", kRemovePickedId);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }
    if (going == 0) {
        // What was selected went while the question was open, to an undo or the account reloading.
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    char count[kMessageCapacity]{};
    (void)std::snprintf(count, sizeof count, "%zu Items", going);
    const char* what = !one ? count : definition != nullptr ? definition->name.c_str() : "Unknown Item";
    const char* back = one ? kUndoOneNote : kUndoManyNote;
    char note[kMessageCapacity]{};
    if (kept == 0) {
        (void)std::snprintf(note, sizeof note, "%s", back);
    } else {
        (void)std::snprintf(note,
                            sizeof note,
                            "%zu locked or equipped %s. %s",
                            kept,
                            kept == 1 ? "item stays" : "items stay",
                            back);
    }
    const bool confirmed = confirm_removal(what, note);
    ImGui::EndPopup();
    return confirmed;
}

/** @return The width of the bar's count slot, which is as wide as three figures whatever the count. */
[[nodiscard]] float pick_count_width() noexcept {
    return ImGui::CalcTextSize(art::shout(kPickCountSample).c_str()).x;
}

/**
 * @return How far the bar's count and actions run, from its left inset to the far edge of Remove.
 * Every action keeps its place whatever is selected, disabled when nothing selected takes it, so this
 * is the same with nothing selected as with anything, and the bar is laid out from it before either.
 */
[[nodiscard]] float pick_actions_width() noexcept {
    const Model& state = model();
    const state::AccountState& account = state.draft->after;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float gap = pixels(controls::kGroupGap);
    float width = pick_count_width() + gap + button_width(kLockLabel) + spacing + button_width(kUnlockLabel);
    if (account.characterCount > 1) {
        width += gap + ImGui::CalcTextSize(kSendLabel).x;
        for (std::size_t c = 0; c < account.characterCount; ++c) {
            if (c != state.character) {
                width += spacing + button_width(character_label(c).c_str());
            }
        }
    }
    return width + gap + pixels(kPickPowerWidth) + spacing + button_width(kSetPowerLabel) + gap
           + button_width(kPullLabel) + gap + button_width(kRemoveLabel);
}

/**
 * Draws the bar's count and its actions from the cursor, as `pick_actions_width` measures them. An
 * action nothing selected takes keeps its place, disabled, with the reason under the pointer.
 * @return The action pressed.
 */
[[nodiscard]] Bulk draw_pick_actions() noexcept {
    Bulk bulk;
    Model& state = model();
    const state::AccountState& account = state.draft->after;
    const float gap = pixels(controls::kGroupGap);
    char count[kMessageCapacity]{};
    (void)std::snprintf(count, sizeof count, "%zu Selected", state.picked.size());
    ImGui::AlignTextToFramePadding();
    tooltip::draw_label(count);
    ImGui::SameLine(0.0F, (std::max)(0.0F, pick_count_width() - ImGui::GetItemRectSize().x) + gap);
    if (ImGui::Button(kLockLabel)) {
        bulk.kind = Bulk::Kind::lock;
    }
    ImGui::SameLine();
    if (ImGui::Button(kUnlockLabel)) {
        bulk.kind = Bulk::Kind::unlock;
    }
    // Sending reads as the pane's own send row: the words, then a button for each other character.
    // One none of the selection could go to keeps its button, disabled, so the row says why.
    if (account.characterCount > 1) {
        ImGui::SameLine(0.0F, gap);
        ImGui::TextUnformatted(kSendLabel);
        for (std::size_t c = 0; c < account.characterCount; ++c) {
            if (c == state.character) {
                continue;
            }
            const bool fits = std::any_of(state.picked.begin(), state.picked.end(), [&](std::uint64_t instance) {
                const edit::Item* item = find_owned_item(instance);
                const edit::CatalogItem* definition = item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
                return definition != nullptr && !item->postmaster && !is_equipped(instance)
                       && edit::fits_class(*definition, account.characters[c].characterClass);
            });
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(c));
            ImGui::BeginDisabled(!fits);
            if (ImGui::Button(character_label(c).c_str())) {
                bulk = {Bulk::Kind::send, c};
            }
            ImGui::EndDisabled();
            if (!fits && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("None can go there: equipped, at the Postmaster or the wrong class.");
            }
            ImGui::PopID();
        }
    }
    // Power, the pull and the removal keep their places too, so the row does not reflow as the
    // selection changes what they can act on.
    const bool powered = std::any_of(state.picked.begin(), state.picked.end(), [&state](std::uint64_t instance) {
        const edit::Item* item = find_owned_item(instance);
        const edit::CatalogItem* definition = item != nullptr ? state.catalog.find(item->definitionHash) : nullptr;
        return definition != nullptr && carries_power(*definition);
    });
    const bool waiting = std::any_of(state.picked.begin(), state.picked.end(), [](std::uint64_t instance) {
        const edit::Item* item = find_owned_item(instance);
        return item != nullptr && item->postmaster;
    });
    const bool removing = std::any_of(state.picked.begin(), state.picked.end(), can_remove);
    ImGui::SameLine(0.0F, gap);
    ImGui::BeginDisabled(!powered);
    ImGui::SetNextItemWidth(pixels(kPickPowerWidth));
    (void)ImGui::InputInt("##pick_power", &state.pickPower, 0, 0);
    // The field has no label of its own beside it; the button after it names what it is for.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", powered ? "Power to set the selected weapons and armor to." : kPowerlessTip);
    }
    const bool entered = ImGui::IsItemDeactivated()
                         && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    state.pickPower = std::clamp(state.pickPower, 0, kPowerSliderMaximum);
    ImGui::SameLine();
    if (ImGui::Button(kSetPowerLabel) || entered) {
        bulk.kind = Bulk::Kind::power;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", powered ? "Set selected gear to this Power." : kPowerlessTip);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0F, gap);
    ImGui::BeginDisabled(!waiting);
    if (ImGui::Button(kPullLabel)) {
        bulk.kind = Bulk::Kind::pull;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", waiting ? "Pull selected items from the Postmaster." : kNotWaitingTip);
    }
    ImGui::SameLine(0.0F, gap);
    ImGui::BeginDisabled(!removing);
    if (ImGui::Button(kRemoveLabel)) {
        ImGui::OpenPopup(kRemovePickedId);
    }
    ImGui::EndDisabled();
    if (!removing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", kKeptTip);
    }
    return bulk;
}

/**
 * Draws the bar that stands over the grid while anything is selected: how many items are, what can
 * be done to all of them, and at its far end the ways to widen the selection or let it go.
 * @param shown Items the page's filters show, which Select All Shown takes.
 * @return The action pressed, which the page carries out once it has drawn its items: sending or
 * removing moves the items the grid is about to read.
 */
[[nodiscard]] Bulk draw_pick_bar(const std::vector<const edit::Item*>& shown) noexcept {
    Bulk bulk;
    Model& state = model();
    if (!state.picked.empty()) {
        controls::space(controls::kRowSpacing);
        const ImGuiStyle& style = ImGui::GetStyle();
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = (std::max)(0.0F, ImGui::GetContentRegionAvail().x - overlay_width());
        const float padding = pixels(kPickBarPadding);
        const float inset = pixels(kPickBarInset);
        const float line = ImGui::GetFrameHeight();
        // Select All Shown and Clear Selection sit at the far end, apart from what acts on the items,
        // or on a line of their own when the actions leave them no room. The actions measure the same
        // whatever is selected, so that turns on the width alone.
        const float trailing = button_width(kSelectAllLabel) + style.ItemSpacing.x + button_width(kClearLabel);
        const bool wrapped = (inset * 2.0F) + pick_actions_width() + style.ItemSpacing.x + trailing > width;
        const float second = origin.y + padding + line + style.ItemSpacing.y;
        const float bottom = (wrapped ? second + line : origin.y + padding + line) + padding;
        // The controls are laid out first and the strip's fill goes in behind them afterwards.
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos({origin.x + inset, origin.y + padding});
        bulk = draw_pick_actions();
        ImGui::SameLine();
        const float trailingLeft = (std::max)(ImGui::GetCursorScreenPos().x, origin.x + width - inset - trailing);
        ImGui::SetCursorScreenPos(wrapped ? ImVec2{origin.x + inset, second} : ImVec2{trailingLeft, origin.y + padding});
        if (ImGui::Button(kSelectAllLabel)) {
            for (const edit::Item* item : shown) {
                if (!is_picked(item->instanceSoid)) {
                    state.picked.push_back(item->instanceSoid);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(kClearLabel)) {
            state.picked.clear();
        }
        draw->ChannelsSetCurrent(0);
        const ImVec2 corner{origin.x + width, bottom};
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_Header), pixels(controls::kRowRounding));
        draw->AddRectFilled(origin, {origin.x + pixels(controls::kRailWidth), bottom}, ImGui::GetColorU32(ImGuiCol_CheckMark));
        draw->ChannelsMerge();
        ImGui::SetCursorScreenPos({origin.x, bottom});
        ImGui::Dummy({width, 0.0F});
    }
    // The question is drawn whatever is selected now, so one an undo emptied the selection under
    // closes itself rather than waiting for the next selection to bring it back.
    if (draw_remove_picked_modal()) {
        bulk.kind = Bulk::Kind::remove;
    }
    return bulk;
}

/**
 * @return The height of one row of the page's two lists, the profile's stacks and the search's other
 * results alike, on whole pixels: the taller of the icon and a field, with the row's padding over and
 * under it. The lines are set edge to edge, so this is also the pitch both clippers seek by, and the
 * two lists read as one when a page shows both.
 */
[[nodiscard]] float list_row_height() noexcept {
    return std::ceil((std::max)(pixels(kProfileIconExtent), ImGui::GetFrameHeight())
                     + (pixels(kProfileRowPadding) * 2.0F));
}

/**
 * Paints the ground of one list row: nothing at rest, and the fill it takes while it is lit. The
 * game lists its currencies and materials as plain rows, with nothing filled in until the pointer
 * reaches a row, and both lists take their ground from here so they answer the pointer alike.
 * @param lo Top-left of the row in screen space.
 * @param hi Bottom-right of the row in screen space.
 * @param lit True under the pointer, or while the row's own menu or question is open.
 */
void draw_row_ground(ImVec2 lo, ImVec2 hi, bool lit) noexcept {
    if (lit) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            lo, hi, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), pixels(controls::kRowRounding));
    }
}

/** One search result kept somewhere other than the page's own list. */
struct Hit {
    /** Character that keeps it, or `kAccountItems` for an account stack. */
    std::size_t owner{kAccountItems};
    const edit::CatalogItem* definition{};
    /** The item a character keeps, or null for a stack. */
    const edit::Item* item{};
    /** Where the owner keeps it: equipped, at the postmaster, or neither. */
    bool equipped{};
    bool postmaster{};
    int quantity{};
};

/**
 * @return Every item matching a search that the page does not already list: what each other
 * character keeps, and the account's stacks when asked for.
 * @param skip Character whose items the page lists itself, or `kAccountItems` to leave none out.
 */
[[nodiscard]] std::vector<Hit> search_elsewhere(const std::string& search, std::size_t skip, bool stacks) noexcept {
    const Model& state = model();
    const state::AccountState& account = state.draft->after;
    const Query query = read_query(search);
    std::vector<Hit> hits;
    const auto consider = [&](std::size_t owner, const edit::Item& item, bool equipped) {
        const edit::CatalogItem* definition = state.catalog.find(item.definitionHash);
        if (definition != nullptr && passes(query, *definition, &item, equipped)) {
            hits.push_back({owner, definition, &item, equipped, item.postmaster, item.quantity});
        }
    };
    for (std::size_t c = 0; c < account.characterCount; ++c) {
        if (c == skip) {
            continue;
        }
        const state::CharacterState& other = account.characters[c];
        for (const auto& slot : other.equipment.slots) {
            if (slot) {
                consider(c, *slot, true);
            }
        }
        for (std::size_t i = 0; i < other.inventory.count; ++i) {
            consider(c, other.inventory.values[i], false);
        }
    }
    for (std::size_t i = 0; stacks && i < account.profileItemCount; ++i) {
        const auto& stack = account.profileItems[i];
        const edit::CatalogItem* definition = state.catalog.find(stack.definitionHash);
        if (definition != nullptr && passes(query, *definition, nullptr, false)) {
            hits.push_back({kAccountItems, definition, nullptr, false, false, stack.quantity});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.definition->name < b.definition->name;
    });
    return hits;
}

/**
 * Takes the page to where one result is kept: the character that keeps it, with the item open in
 * the pane and the search carried over, or the profile page for a stack.
 * @param search The search that found it, which the page it goes to is given.
 */
void go_to(const Hit& hit, const char* search) noexcept {
    Model& state = model();
    if (hit.owner == kAccountItems) {
        state.view = View::profileInventory;
        (void)std::snprintf(state.profileSearch, sizeof state.profileSearch, "%s", search);
        return;
    }
    state.character = hit.owner;
    state.view = View::characterInventory;
    (void)std::snprintf(state.inventorySearch, sizeof state.inventorySearch, "%s", search);
    state.inventorySlot = -1;
    state.picked.clear();
    state.browse.type.clear();
    state.results.key.clear();
    select(*hit.definition, hit.item->instanceSoid);
}

/**
 * Draws one result kept elsewhere as a compact row: the icon, the name, and at the far end who
 * keeps it and where. The item's own tooltip is under the pointer, and the row goes to it.
 * @return True when the row was pressed.
 */
[[nodiscard]] bool draw_hit(const Hit& hit, float width, float rowHeight) noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    const float icon = pixels(kProfileIconExtent);
    const float inset = pixels(kProfileRowInset);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner{origin.x + width, origin.y + rowHeight};
    const bool clicked = ImGui::InvisibleButton("hit", {width, rowHeight});
    const bool hovered = ImGui::IsItemHovered();
    draw_row_ground(origin, corner, hovered);
    art::icon(*hit.definition, {origin.x + inset, origin.y + ((rowHeight - icon) * 0.5F)}, icon);

    std::string where = hit.owner == kAccountItems ? std::string(kProfileLabel) : character_label(hit.owner);
    if (hit.equipped) {
        where += controls::kDetailSeparator;
        where += kEquippedWhere;
    } else if (hit.postmaster) {
        where += controls::kDetailSeparator;
        where += kPostmasterWhere;
    } else if (hit.item == nullptr || hit.quantity > 1) {
        where += controls::kDetailSeparator;
        where += std::to_string(hit.quantity);
    }
    const float whereWidth = ImGui::CalcTextSize(where.c_str()).x;
    const float text = origin.y + ((rowHeight - ImGui::GetTextLineHeight()) * 0.5F);
    draw->AddText({corner.x - inset - whereWidth, text}, ImGui::GetColorU32(tooltip::muted()), where.c_str());
    const float nameLeft = origin.x + inset + icon + inset;
    art::clipped_text(hit.definition->name,
                      {nameLeft, text},
                      (std::max)(0.0F, corner.x - inset - whereWidth - inset - nameLeft),
                      ImGui::GetColorU32(ImGuiCol_Text));
    if (hovered) {
        tooltip::draw(*hit.definition, hit.item);
    }
    return clicked;
}

/**
 * Draws what a search finds elsewhere on the account, under the page's own results, in a section
 * of its own that folds like any other. Pressing a result takes the page to it.
 * @param query The search, already folded for matching.
 * @param search The search as typed, which the page a result is on is given.
 * @param skip Character whose items the page lists itself, or `kAccountItems`.
 * @param stacks True to include the account's stacks, which the account page lists itself.
 * @return True when anything was found.
 */
bool draw_elsewhere(const std::string& query, const char* search, std::size_t skip, bool stacks) noexcept {
    if (query.empty()) {
        return false;
    }
    const std::vector<Hit> hits = search_elsewhere(query, skip, stacks);
    if (hits.empty()) {
        return false;
    }
    controls::space(controls::kSectionSpacing);
    if (!controls::section_header(kElsewhereLabel, hits.size())) {
        return true;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    int columns = 1;
    float width = 0.0F;
    card_grid(ImGui::GetContentRegionAvail().x, spacing, pixels(kProfileRowMinimumWidth), columns, width);
    const auto perRow = static_cast<std::size_t>(columns);
    const std::size_t lines = (hits.size() + perRow - 1) / perRow;
    const float rowHeight = list_row_height();
    const Hit* pressed = nullptr;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{spacing, 0.0F});
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines), rowHeight);
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            for (std::size_t column = 0; column < perRow; ++column) {
                const std::size_t index = (static_cast<std::size_t>(line) * perRow) + column;
                if (index >= hits.size()) {
                    break;
                }
                if (column != 0) {
                    ImGui::SameLine(0.0F, spacing);
                }
                ImGui::PushID(static_cast<int>(index));
                if (draw_hit(hits[index], width, rowHeight)) {
                    pressed = &hits[index];
                }
                ImGui::PopID();
            }
        }
    }
    clipper.End();
    ImGui::PopStyleVar();
    // The page changes character or view on a press, so the jump waits until the list is drawn.
    if (pressed != nullptr) {
        const std::string typed = search;
        go_to(*pressed, typed.c_str());
    }
    return true;
}

/** Draws the slot picker that narrows the character item list. */
void draw_slot_picker() noexcept {
    Model& state = model();
    const char* preview =
        state.inventorySlot < 0 ? "All Slots" : edit::kSlots[state.inventorySlot];
    if (!controls::begin_picker("##inventory_slot", preview, pixels(controls::kSlotPickerWidth))) {
        return;
    }
    if (controls::picker_row("All Slots", state.inventorySlot < 0)) {
        state.inventorySlot = -1;
    }
    for (int slot = 0; slot < static_cast<int>(kSlotCount); ++slot) {
        if (controls::picker_row(edit::kSlots[slot], state.inventorySlot == slot)) {
            state.inventorySlot = slot;
        }
    }
    controls::end_picker();
}

/**
 * @return The group one stored item belongs to.
 * Buckets carry no name in the installed build, so gear is grouped by the equipment slot its
 * bucket feeds and everything else by its own item type, which is what the game calls it.
 */
[[nodiscard]] std::string item_group(const edit::Item& item,
                                     const edit::CatalogItem& definition) noexcept {
    if (item.postmaster) {
        return "Postmaster";
    }
    if (definition.slot < inv::kEquipmentSlotCount) {
        return edit::kSlots[definition.slot];
    }
    return definition.type.empty() ? std::string("Other") : definition.type;
}

/** The +1 and +2 bucket ranks only sit past the equipment run while the two counts agree. */
static_assert(kSlotCount == inv::kEquipmentSlotCount,
              "Bucket ranks assume the module and the account agree on the slot count.");

/**
 * Draws a page's count in the muted capitals the editor counts a list in: how many the page holds,
 * or how many of them show while a search or a filter narrows it.
 * @param shown Rows the page's filters let through.
 * @param held Rows the page holds in all.
 * @param one What one row is, such as "Item".
 * @param many What several are, such as "Items".
 */
void draw_count(std::size_t shown, std::size_t held, const char* one, const char* many) noexcept {
    char count[kMessageCapacity]{};
    if (shown == held) {
        (void)std::snprintf(count, sizeof count, "%zu %s", held, held == 1 ? one : many);
    } else {
        (void)std::snprintf(count, sizeof count, "%zu of %zu", shown, held);
    }
    tooltip::draw_label(count);
}

/** Draws everything the character carries, equipped and stowed, divided into its buckets. */
void draw_character_items() noexcept {
    Model& state = model();
    prune_picks();
    // Escape lets the selection go, unless something else is taking the key: a field being typed in,
    // or a list or a question open over the page. It is read before anything on the page answers it.
    if (!state.picked.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        state.picked.clear();
    }
    ImGui::SetNextItemWidth(pixels(kSearchWidth));
    (void)controls::search("##inventory_search",
                           "Search Inventory...",
                           state.inventorySearch,
                           sizeof state.inventorySearch);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kSearchTip);
    }
    ImGui::SameLine();
    draw_slot_picker();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    // The list is built once, before the child, so the count and the grid agree on the filters.
    const std::vector<const edit::Item*> items = filtered_character_items();
    draw_count(items.size(), character().inventory.count + equipped_count(), "Item", "Items");
    draw_add_action(Category::weapons, state.inventorySlot);
    // Sending or removing moves the items the grid is about to read, so an action on the selection
    // waits until the grid is drawn.
    const Bulk bulk = draw_pick_bar(items);

    if (!ImGui::BeginChild("owned")) {
        ImGui::EndChild();
        run_bulk(bulk);
        return;
    }
    std::map<std::string, std::vector<const edit::Item*>> groups;
    std::map<std::string, std::size_t> order;
    for (const edit::Item* item : items) {
        const edit::CatalogItem* definition = state.catalog.find(item->definitionHash);
        if (definition == nullptr) {
            continue;
        }
        const std::string group = item_group(*item, *definition);
        groups[group].push_back(item);
        // Rank is decided from the same three cases the name is, so one cannot disagree with the
        // other. `emplace` is first-wins, and the Postmaster is the one bucket whose name is fixed
        // while its rank was taken from whichever item reached it first: a postmastered hand
        // cannon ranked it 0, which floated it up beside Kinetic.
        order.emplace(group,
                      item->postmaster ? kSlotCount + 2
                      : definition->slot < inv::kEquipmentSlotCount
                          ? definition->slot
                      : definition->type.empty() ? kSlotCount + 1
                                                 : kSlotCount);
    }
    std::vector<std::pair<std::string, std::vector<const edit::Item*>>> ordered;
    ordered.reserve(groups.size());
    for (auto& entry : groups) {
        ordered.emplace_back(entry.first, std::move(entry.second));
    }
    std::sort(ordered.begin(), ordered.end(), [&order](const auto& a, const auto& b) {
        const std::size_t left = order[a.first];
        const std::size_t right = order[b.first];
        return left == right ? a.first < b.first : left < right;
    });

    // One bucket per collapsing section, each holding the same responsive card grid the loadout
    // uses. Sundial lays its character inventory out this way; splitting the page into fixed
    // columns of single-column lists wasted most of the width whatever the tiles were sized at.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    bool leading = true;
    for (const auto& [group, members] : ordered) {
        // Every section after the first is set apart by the page's section gap, open or folded.
        if (!leading) {
            controls::space(controls::kSectionSpacing);
        }
        leading = false;
        if (!controls::section_header(group.c_str(), members.size())) {
            continue;
        }
        int columns = 1;
        float width = 0.0F;
        card_grid(ImGui::GetContentRegionAvail().x, spacing, pixels(kCardMinimumWidth), columns, width);
        // Every card is resolved before the grid runs. Re-resolving inside it meant a null could
        // `continue` after SameLine had already fired, which left the next card taking the skipped
        // card's cell and the column accounting drifting for the rest of the bucket; and the row
        // height was being measured on the pre-edit pointer while the card drew from the resolved
        // one, so an equip inside the bucket could clip a card's own socket row.
        std::vector<edit::Item*> live;
        live.reserve(members.size());
        for (const edit::Item* member : members) {
            if (edit::Item* resolved = find_owned_item(member->instanceSoid)) {
                live.push_back(resolved);
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{spacing, pixels(kCardColumnSpacing)});
        const auto perRow = static_cast<std::size_t>(columns);
        float rowHeight = 0.0F;
        for (std::size_t i = 0; i < live.size(); ++i) {
            if (i % perRow == 0) {
                // The row is drawn at its tallest card, so a bucket of socketless items packs in.
                rowHeight = 0.0F;
                for (std::size_t j = i; j < live.size() && j < i + perRow; ++j) {
                    rowHeight = (std::max)(rowHeight, card::natural_height(live[j], width));
                }
            } else {
                ImGui::SameLine(0.0F, spacing);
            }
            edit::Item* item = live[i];
            const edit::CatalogItem* definition = state.catalog.find(item->definitionHash);
            const bool equipped = is_equipped(item->instanceSoid);
            // The bucket already names the slot, so the card only says when the item is equipped.
            card::draw(item,
                       equipped ? "Equipped" : nullptr,
                       definition != nullptr ? definition->slot : kSlotCount,
                       equipped         ? card::Action::swap
                       : item->postmaster ? card::Action::pull
                                          : card::Action::equip,
                       width,
                       rowHeight);
        }
        ImGui::PopStyleVar();
    }
    const std::string query = edit::searchable(state.inventorySearch);
    if (items.empty()) {
        // A search or the slot picker is what emptied the page; with neither, the character has nothing.
        const bool narrowed = !query.empty() || state.inventorySlot >= 0;
        ImGui::TextColored(tooltip::muted(), "%s", narrowed ? kNoMatchLabel : "No items on this character.");
    }
    // A search reaches past this character: what the others keep, and the account's stacks.
    (void)draw_elsewhere(query, state.inventorySearch, state.character, true);
    ImGui::EndChild();
    run_bulk(bulk);
}

/** Removes one account stack from the draft, keeping the remaining rows packed. */
void erase_profile_item(std::size_t index) noexcept {
    state::AccountState& account = model().draft->after;
    for (std::size_t row = index + 1; row < account.profileItemCount; ++row) {
        account.profileItems[row - 1] = account.profileItems[row];
    }
    account.profileItems[--account.profileItemCount] = {};
    mark_changed();
}

/**
 * Draws one profile stack as a single compact row.
 * Everything is laid out through the normal cursor: the icon, a spacer that reserves the name
 * column, then the controls. Each part is set on the row's middle by moving the cursor down before
 * the item that follows it, never after, since a cursor moved and left without an item behind it
 * left the parent unable to size itself, which Dear ImGui reports and which broke the later columns.
 * @param index Row in the draft profile list.
 * @param width Row width in framebuffer pixels.
 * @param rowHeight Row height in framebuffer pixels, the pitch the caller's clipper seeks by, so the
 * lines it skips are the same height as the lines it draws.
 * @return True when the stack's removal was confirmed. The caller erases it after the loop.
 */
[[nodiscard]] bool draw_profile_item(std::size_t index, float width, float rowHeight) noexcept {
    Model& state = model();
    state::account::inventory::ProfileItem& item = state.draft->after.profileItems[index];
    const edit::CatalogItem* definition = state.catalog.find(item.definitionHash);
    const float icon = pixels(kProfileIconExtent);
    const float inset = pixels(kProfileRowInset);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float quantityWidth = pixels(kQuantityWidth);
    const float removeWidth = button_width(kRemoveLabel);
    const float nameWidth = (std::max)(
        0.0F, width - (inset * 3.0F) - icon - quantityWidth - removeWidth - (spacing * 2.0F));

    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginGroup();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner{origin.x + width, origin.y + rowHeight};
    // A held button blocks the window's hover, and the Remove button is shown only on a hovered row:
    // without the flag it vanished the moment it was pressed, so its release never landed.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                         && ImGui::IsMouseHoveringRect(origin, corner);
    // The row stays lit while its menu or its question is open, so it is plain which row they ask about.
    const bool asked = ImGui::IsPopupOpen(kStackMenuId) || ImGui::IsPopupOpen(kRemoveStackTitle);
    // The row's extent is known before its content, so its ground goes in first: the draw list paints
    // in order, and a fill painted after the group had measured itself covered the icon and the name.
    draw_row_ground(origin, corner, hovered || asked);
    const float middle = origin.y + (rowHeight * 0.5F);

    ImGui::Dummy({inset, rowHeight});
    ImGui::SameLine(0.0F, 0.0F);
    if (definition != nullptr) {
        art::icon(*definition, {ImGui::GetCursorScreenPos().x, middle - (icon * 0.5F)}, icon);
    }
    ImGui::Dummy({icon, rowHeight});

    ImGui::SameLine(0.0F, inset);
    const float nameLeft = ImGui::GetCursorScreenPos().x;
    art::clipped_text(definition != nullptr ? definition->name : std::string("Unknown Item"),
                      {nameLeft, middle - (ImGui::GetTextLineHeight() * 0.5F)},
                      nameWidth,
                      ImGui::GetColorU32(definition != nullptr ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::Dummy({nameWidth, rowHeight});
    // The item reads the same here as it does anywhere else in the editor. The controls at the far
    // end are excluded, so the tooltip does not stand over the field the player is reaching for.
    if (hovered && definition != nullptr && ImGui::GetIO().MousePos.x < nameLeft + nameWidth) {
        tooltip::draw(*definition, nullptr);
    }

    // The field and the removal sit on the row's middle, as the icon and the name do.
    const float field = middle - (ImGui::GetFrameHeight() * 0.5F);
    ImGui::SameLine(0.0F, spacing);
    ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, field});
    const int limit = definition != nullptr ? (std::max)(1, definition->detail.maxStackSize)
                                            : kUnknownStackLimit;
    ImGui::SetNextItemWidth(quantityWidth);
    // An empty label keeps Dear ImGui from printing one after the stepper buttons.
    const bool changed = ImGui::InputInt("##quantity", &item.quantity, 0, 0);
    if (changed) {
        item.quantity = std::clamp(item.quantity, 1, limit);
    }
    record_scalar_edit(changed);

    ImGui::SameLine(0.0F, spacing);
    ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, field});
    // The removal is offered only on the row under the pointer, so a page of stacks does not read
    // as a page of Remove buttons. Its width is reserved either way, so the columns hold still.
    bool ask = false;
    if (hovered || asked) {
        ask = ImGui::Button(kRemoveLabel);
    } else {
        ImGui::Dummy({removeWidth, ImGui::GetFrameHeight()});
    }
    // The row's own menu offers the same removal, as a card's menu offers a card's.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(kStackMenuId);
    }
    if (controls::begin_menu(kStackMenuId)) {
        ask = ImGui::Selectable(kRemoveLabel) || ask;
        controls::end_menu();
    }
    // The question is opened once the menu is shut: opened from inside it, it closed with the menu.
    if (ask) {
        ImGui::OpenPopup(kRemoveStackTitle);
    }

    bool removed = false;
    if (ImGui::BeginPopupModal(kRemoveStackTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        removed = confirm_removal(definition != nullptr ? definition->name.c_str() : "Unknown Item", kUndoOneNote);
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return removed;
}

/**
 * @return The section one account stack belongs under, which is what the game calls the item.
 * The profile holds currencies, materials, mods and shaders all in one run of rows, so its own
 * order says nothing a player is looking for; the type does.
 */
[[nodiscard]] std::string profile_group(const edit::CatalogItem* definition) noexcept {
    if (definition == nullptr) {
        return kUnknownGroup;
    }
    return definition->type.empty() ? std::string(kUntypedGroup) : definition->type;
}

/** One section of the account page: its heading, and the draft rows filed under it. */
struct ProfileGroup {
    std::string name;
    std::vector<std::size_t> rows;
};

/**
 * @return Every account stack matching the page search, gathered into its sections.
 * Sections read alphabetically, with the two catch-alls last so a named type is never buried
 * under them, and each section's rows read by name.
 */
[[nodiscard]] std::vector<ProfileGroup> grouped_profile_items() noexcept {
    Model& state = model();
    const std::string search = edit::searchable(state.profileSearch);
    const Query query = read_query(search);
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < state.draft->after.profileItemCount; ++i) {
        const edit::CatalogItem* definition =
            state.catalog.find(state.draft->after.profileItems[i].definitionHash);
        // A stack the catalog does not carry still has to be reachable, so an empty search keeps
        // it and any typed search drops it: there is no name to match it against.
        if (!search.empty() && (definition == nullptr || !passes(query, *definition, nullptr, false))) {
            continue;
        }
        groups[profile_group(definition)].push_back(i);
    }

    const auto rank = [](const std::string& name) {
        return name == kUntypedGroup ? 1 : name == kUnknownGroup ? 2 : 0;
    };
    const auto byName = [&state](std::size_t left, std::size_t right) {
        const edit::CatalogItem* first =
            state.catalog.find(state.draft->after.profileItems[left].definitionHash);
        const edit::CatalogItem* second =
            state.catalog.find(state.draft->after.profileItems[right].definitionHash);
        if (first == nullptr || second == nullptr) {
            return first != nullptr;
        }
        return first->name < second->name;
    };
    std::vector<ProfileGroup> ordered;
    ordered.reserve(groups.size());
    for (auto& [name, rows] : groups) {
        std::sort(rows.begin(), rows.end(), byName);
        ordered.push_back({name, std::move(rows)});
    }
    std::sort(ordered.begin(), ordered.end(), [&rank](const ProfileGroup& a, const ProfileGroup& b) {
        const int left = rank(a.name);
        const int right = rank(b.name);
        return left == right ? a.name < b.name : left < right;
    });
    return ordered;
}

/**
 * Draws one section of profile stacks as a responsive grid.
 * @param rows Draft indices filed under this section.
 * @param rowHeight Row height in framebuffer pixels, which is also the pitch of its lines.
 * @param removal Receives the draft index whose removal was confirmed, if any.
 */
void draw_profile_group(const std::vector<std::size_t>& rows,
                        float rowHeight,
                        std::size_t& removal) noexcept {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    int columns = 1;
    float width = 0.0F;
    card_grid(ImGui::GetContentRegionAvail().x, spacing, pixels(kProfileRowMinimumWidth), columns, width);
    const auto perRow = static_cast<std::size_t>(columns);
    const std::size_t lines = (rows.size() + perRow - 1) / perRow;

    // Lines are set edge to edge and ruled off from one another, as the search's other results are.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{spacing, 0.0F});
    // The profile holds up to 701 stacks. Building all of them cost more per frame than the whole
    // of the rest of the page, so only the lines the player can see are submitted.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines), rowHeight);
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            const std::size_t first = static_cast<std::size_t>(line) * perRow;
            float top = 0.0F;
            for (std::size_t column = 0; column < perRow && first + column < rows.size(); ++column) {
                if (column == 0) {
                    top = ImGui::GetCursorPosY();
                } else {
                    // SameLine carries the baseline of whatever the last row ended on, which left
                    // the columns of one line sitting a couple of pixels apart. Only the X it
                    // works out is wanted, so the line's own top is put back afterwards.
                    ImGui::SameLine(0.0F, spacing);
                    ImGui::SetCursorPosY(top);
                }
                if (draw_profile_item(rows[first + column], width, rowHeight)) {
                    removal = rows[first + column];
                }
            }
        }
    }
    clipper.End();
    ImGui::PopStyleVar();
}

/** Draws every account-wide stack, which belongs to the profile rather than a character. */
void draw_profile_items() noexcept {
    Model& state = model();
    const std::size_t held = state.draft->after.profileItemCount;
    const std::vector<ProfileGroup> groups = grouped_profile_items();
    std::size_t shown = 0;
    for (const ProfileGroup& group : groups) {
        shown += group.rows.size();
    }

    ImGui::SetNextItemWidth(pixels(kSearchWidth));
    (void)controls::search("##profile_search",
                           "Search Profile Items...",
                           state.profileSearch,
                           sizeof state.profileSearch);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kSearchTip);
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    draw_count(shown, held, "Stack", "Stacks");
    draw_add_action(Category::materials, -1);

    if (!ImGui::BeginChild("profile_items")) {
        ImGui::EndChild();
        return;
    }
    const std::string query = edit::searchable(state.profileSearch);
    if (groups.empty()) {
        ImGui::TextColored(tooltip::muted(), "%s", query.empty() ? "No stacks on this profile." : kNoMatchLabel);
        // A search reaches the characters too, whose items this page does not list.
        (void)draw_elsewhere(query, state.profileSearch, kAccountItems, false);
        ImGui::EndChild();
        return;
    }
    const float rowHeight = list_row_height();
    // A confirmed removal moves every later row, so the list is left and erased afterwards.
    std::size_t removal = held;
    bool leading = true;
    for (const ProfileGroup& group : groups) {
        // Every section after the first is set apart by the page's section gap, open or folded.
        if (!leading) {
            controls::space(controls::kSectionSpacing);
        }
        leading = false;
        if (!controls::section_header(group.name.c_str(), group.rows.size())) {
            continue;
        }
        draw_profile_group(group.rows, rowHeight, removal);
    }
    if (removal < held) {
        erase_profile_item(removal);
    }
    (void)draw_elsewhere(query, state.profileSearch, kAccountItems, false);
    ImGui::EndChild();
}

} // namespace

bool is_picked(std::uint64_t instance) noexcept {
    const std::vector<std::uint64_t>& picked = model().picked;
    return std::find(picked.begin(), picked.end(), instance) != picked.end();
}

void toggle_pick(std::uint64_t instance) noexcept {
    std::vector<std::uint64_t>& picked = model().picked;
    const auto at = std::find(picked.begin(), picked.end(), instance);
    if (at == picked.end()) {
        picked.push_back(instance);
    } else {
        picked.erase(at);
    }
}

void draw_character_inventory_page() noexcept {
    draw_character_items();
}

void draw_profile_inventory_page() noexcept {
    draw_profile_items();
}

} // namespace dawn::core::ui::modules::loadout::internal
