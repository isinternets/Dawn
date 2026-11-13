#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <utility>

#include "../../core/logging/log.h"
#include "../../core/settings/settings.h"
#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../build_data/runtime.h"
#include "runtime.h"
#include "state.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"
#include "../account/festival_mask.h"
#include "../activity/nightfall/rules.h"
#include "../persistence/persistence.h"
#include "../unlocks/unlocks_runtime.h"

namespace dawn::state {
namespace runtime::detail {

namespace authored_inventory = account::inventory;
namespace item_details = build_data::items::details;
namespace inventory_buckets = build_data::inventory::buckets;
namespace family4_loadout = middleware::datagen::family4::loadout;

/** Writes one exhaustive equipment-transaction checkpoint to the persistent diagnostic log. */
void report_equipment(std::string_view stage,
                      std::string_view result,
                      EquipmentMutationKind kind,
                      std::uint64_t characterSoid,
                      std::uint64_t previousSoid,
                      std::uint64_t requestedSoid,
                      std::size_t equipmentIndex,
                      std::size_t inventoryIndex,
                      std::uint8_t nativeSlot,
                      std::size_t movedItemCount,
                      std::uint32_t previousHash,
                      std::uint32_t requestedHash) noexcept {
    const std::string_view operation = kind == EquipmentMutationKind::unequip ? "unequip" : "equip";
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=equip operation=%.*s stage=%.*s result=%.*s character=0x%llX previous=0x%llX "
        "requested=0x%llX equipment_index=%zu inventory_index=%zu native_slot=%u "
        "moved_items=%zu previous_hash=0x%08X requested_hash=0x%08X",
        static_cast<int>(operation.size()),
        operation.data(),
        static_cast<int>(stage.size()),
        stage.data(),
        static_cast<int>(result.size()),
        result.data(),
        static_cast<unsigned long long>(characterSoid),
        static_cast<unsigned long long>(previousSoid),
        static_cast<unsigned long long>(requestedSoid),
        equipmentIndex,
        inventoryIndex,
        static_cast<unsigned>(nativeSlot),
        movedItemCount,
        previousHash,
        requestedHash);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Writes one exhaustive item-acquisition transaction checkpoint. */
void report_acquisition(std::string_view stage,
                        std::string_view result,
                        std::string_view reason,
                        std::uint32_t definitionHash,
                        std::uint64_t characterSoid,
                        std::uint64_t instanceSoid,
                        std::size_t inventoryIndex,
                        std::uint16_t inventoryRow,
                        std::uint8_t equipmentSlot,
                        std::uint32_t nextInventorySerial) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=acquire stage=%.*s result=%.*s reason=%.*s definition_hash=0x%08X character=0x%llX "
        "instance=0x%llX inventory_index=%zu inventory_row=%u equipment_slot=%u next_serial=%u",
        static_cast<int>(stage.size()),
        stage.data(),
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        definitionHash,
        static_cast<unsigned long long>(characterSoid),
        static_cast<unsigned long long>(instanceSoid),
        inventoryIndex,
        static_cast<unsigned>(inventoryRow),
        static_cast<unsigned>(equipmentSlot),
        nextInventorySerial);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

} // namespace runtime::detail

using namespace runtime::detail;

/** Stores the active account key without publishing an incomplete account. */
bool set_primary_soid(std::uint64_t primarySoid) noexcept {
    if (primarySoid == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    candidate.primarySoid = primarySoid;
    // Characters belong to the account key the Client uses. The reference account and its
    // characters differ only in the low byte, so the authored rows are rebased onto that key.
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        candidate.characters[index].soid = primarySoid + 1U + index;
    }
    if (!account::valid(candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!unlocks::rebind_account(runtime::storage::g_state.account,
                                 runtime::storage::g_state.account)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!persistence::commit_account(runtime::storage::g_state.account, candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!unlocks::rebind_account(runtime::storage::g_state.account, candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    // Publish only after the settings, identity, and durable write hold together.
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

/** Moves the selection to one authored character. */
bool set_selected_character(std::uint64_t characterSoid, bool& changed) noexcept {
    changed = false;
    if (characterSoid == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    std::size_t picked = candidate.characterCount;
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        if (candidate.characters[index].soid == characterSoid) {
            picked = index;
        }
    }
    if (picked == candidate.characterCount) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    const bool alreadySelected = candidate.characters[picked].selected;
    for (CharacterState& character : candidate.characters) {
        character.selected = false;
    }
    candidate.characters[picked].selected = true;
    if (!account::valid(candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!persistence::commit_account(runtime::storage::g_state.account, candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    // Publish only after the whole account is valid and durable.
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    changed = !alreadySelected;
    return true;
}

namespace {

/** Writes one character-creation outcome to the persistent diagnostic log. */
void report_character_creation(std::string_view result,
                               std::string_view reason,
                               CharacterRace race,
                               CharacterGender gender,
                               CharacterClass characterClass,
                               std::size_t index,
                               std::uint64_t soid) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=account stage=create_character result=%.*s reason=%.*s class=%u race=%u gender=%u "
        "index=%zu soid=0x%llX",
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(characterClass),
        static_cast<unsigned>(race),
        static_cast<unsigned>(gender),
        index,
        static_cast<unsigned long long>(soid));
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

} // namespace

/** Adds one character built from the authored template of the requested class. */
bool create_character(CharacterRace race,
                      CharacterGender gender,
                      CharacterClass characterClass,
                      std::uint64_t& characterSoid) noexcept {
    characterSoid = 0;
    const AccountState& templates = core::settings::get().characterTemplates;
    const CharacterState* source = nullptr;
    for (std::size_t index = 0;
         index < templates.characterCount && index < templates.characters.size();
         ++index) {
        if (templates.characters[index].characterClass == characterClass) {
            source = &templates.characters[index];
            break;
        }
    }
    if (source == nullptr) {
        report_character_creation("fail", "no_template", race, gender, characterClass, 0, 0);
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    const std::size_t index = candidate.characterCount;
    const auto refuse = [&](std::string_view reason) noexcept {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        report_character_creation("fail", reason, race, gender, characterClass, index, 0);
        return false;
    };
    if (candidate.primarySoid == 0) {
        return refuse("no_account");
    }
    if (index >= candidate.characters.size()) {
        return refuse("no_free_slot");
    }

    // A character key follows the account key, which is also the form a later sign-on rebases to.
    // A deleted character leaves its key free, and the others keep theirs, so the first free key of
    // the sequence is the one to take.
    std::uint64_t soid = 0;
    for (std::size_t slot = 0; soid == 0 && slot < candidate.characters.size(); ++slot) {
        const std::uint64_t key = candidate.primarySoid + 1U + slot;
        if (!account_owns_soid(candidate, key)) {
            soid = key;
        }
    }
    if (soid == 0) {
        return refuse("key_in_use");
    }
    CharacterState& created = candidate.characters[index];
    created = *source;
    created.soid = soid;
    // A created character is selected as it is created: the Client leaves its sign-in step for the
    // game once the account names a selection. Any earlier selection is released.
    for (CharacterState& other : candidate.characters) {
        other.selected = false;
    }
    created.selected = true;
    created.race = race;
    created.gender = gender;
    // Items get fresh instances, so characters made from one template do not share them. The
    // template's keys are cleared first so they cannot collide with the new ones.
    for (std::optional<account::inventory::Item>& item : created.equipment.slots) {
        if (item.has_value()) {
            item->instanceSoid = 0;
        }
    }
    for (std::size_t itemIndex = 0; itemIndex < created.inventory.count; ++itemIndex) {
        created.inventory.values[itemIndex].instanceSoid = 0;
    }
    candidate.characterCount = index + 1;

    std::uint32_t serial = 0;
    for (std::optional<account::inventory::Item>& item : created.equipment.slots) {
        if (!item.has_value()) {
            continue;
        }
        std::uint64_t instanceSoid = 0;
        if (!next_item_instance_soid(candidate, instanceSoid)) {
            return refuse("item_key");
        }
        item->instanceSoid = instanceSoid;
        item->mutationSerial = static_cast<std::int32_t>(serial++);
    }
    for (std::size_t itemIndex = 0; itemIndex < created.inventory.count; ++itemIndex) {
        std::uint64_t instanceSoid = 0;
        if (!next_item_instance_soid(candidate, instanceSoid)) {
            return refuse("item_key");
        }
        created.inventory.values[itemIndex].instanceSoid = instanceSoid;
        created.inventory.values[itemIndex].mutationSerial = static_cast<std::int32_t>(serial++);
    }
    created.nextInventorySerial = serial;

    // A new character starts the New Light introduction, without weapons or travel gear, which is
    // what the account load gives any character that has not started it.
    if (!prepare_newlight_start(candidate)) {
        return refuse("newlight");
    }

    if (!account::valid(candidate)) {
        return refuse("invalid_account");
    }
    if (!persistence::commit_account(runtime::storage::g_state.account, candidate)) {
        return refuse("commit");
    }
    if (!unlocks::append_character(core::settings::get().initialUnlocks, soid)) {
        // The row is already durable, so take it back rather than leave the two views apart.
        (void)persistence::commit_account(candidate, runtime::storage::g_state.account);
        return refuse("unlocks");
    }
    // Publish only after the whole account is valid and durable.
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    characterSoid = soid;
    report_character_creation("ok", "created", race, gender, characterClass, index, soid);
    return true;
}

namespace {

/** Writes one character-deletion outcome to the persistent diagnostic log. */
void report_character_deletion(std::string_view result,
                               std::string_view reason,
                               std::uint64_t soid,
                               std::size_t remaining) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=account stage=delete_character result=%.*s reason=%.*s "
                                    "soid=0x%llX remaining=%zu",
                                    static_cast<int>(result.size()),
                                    result.data(),
                                    static_cast<int>(reason.size()),
                                    reason.data(),
                                    static_cast<unsigned long long>(soid),
                                    remaining);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

} // namespace

/** Removes one character and everything the account stored for it. */
bool delete_character(std::uint64_t characterSoid) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    const auto refuse = [&](std::string_view reason) noexcept {
        const std::size_t remaining = candidate.characterCount;
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        report_character_deletion("fail", reason, characterSoid, remaining);
        return false;
    };
    std::size_t doomed = candidate.characterCount;
    for (std::size_t index = 0; index < candidate.characterCount; ++index) {
        if (candidate.characters[index].soid == characterSoid) {
            doomed = index;
            break;
        }
    }
    if (characterSoid == 0 || doomed == candidate.characterCount) {
        return refuse("unknown");
    }

    // The characters after the removed one move up a slot and keep their keys. Nothing is renamed,
    // so the Client's view of them, and every row stored under their keys, stays valid.
    for (std::size_t index = doomed; index + 1U < candidate.characterCount; ++index) {
        candidate.characters[index] = candidate.characters[index + 1U];
    }
    candidate.characters[candidate.characterCount - 1U] = CharacterState{};
    --candidate.characterCount;

    if (!account::valid(candidate)) {
        return refuse("invalid_account");
    }
    if (!persistence::commit_character_removal(
            runtime::storage::g_state.account, candidate, characterSoid)) {
        return refuse("commit");
    }
    // The durable rows are gone, so the unlock table follows. It can only miss the character if the
    // two views had already drifted, and there is nothing left to undo then.
    const bool unlocksDropped = unlocks::remove_character(characterSoid);
    // Publish only after the whole account is valid and durable.
    runtime::storage::g_state.account = candidate;
    const std::size_t remaining = candidate.characterCount;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    report_character_deletion(
        "ok", unlocksDropped ? "deleted" : "deleted_no_unlocks", characterSoid, remaining);
    return true;
}

/** Prepares one checked equip transition without changing account State. */
bool prepare_equipment_swap(std::uint64_t requestedInstanceSoid,
                            PendingEquipmentSwap& mutation) noexcept {
    mutation = {};
    if (activity::nightfall::equipment_locked()) return false;
    const AccountState account = account_snapshot();
    if (requestedInstanceSoid == 0 || !account::valid(account)) {
        return false;
    }

    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    if (characterIndex == account.characterCount) {
        return false;
    }

    const CharacterState& before = account.characters[characterIndex];
    family4_loadout::ResolvedLoadout beforeLoadout{};
    if (!family4_loadout::resolve(account, characterIndex, beforeLoadout)) {
        return false;
    }

    std::size_t inventoryIndex = before.inventory.count;
    for (std::size_t index = 0; index < before.inventory.count; ++index) {
        if (before.inventory.values[index].instanceSoid != requestedInstanceSoid) {
            continue;
        }
        if (inventoryIndex != before.inventory.count) {
            return false;
        }
        inventoryIndex = index;
    }
    if (inventoryIndex == before.inventory.count) {
        return false;
    }

    const authored_inventory::Item& requested = before.inventory.values[inventoryIndex];
    std::uint8_t requestedNativeSlot = 0;
    std::size_t equipmentSlotIndex = authored_inventory::kEquipmentSlotCount;
    ResolvedPosition requestedPosition{};
    if (!native_equipment_slot(requested, requestedNativeSlot)
        || !semantic_equipment_slot(requestedNativeSlot, equipmentSlotIndex)
        || !find_resolved_position(beforeLoadout, requestedInstanceSoid, requestedPosition)
        || requestedPosition.equipped || requestedPosition.equipmentSlot != requestedNativeSlot) {
        return false;
    }

    CharacterState after = before;
    auto& equipped = after.equipment.slots[equipmentSlotIndex];
    std::uint64_t previousInstanceSoid = 0;
    std::uint32_t previousDefinitionHash = 0;
    if (equipped.has_value()) {
        std::uint8_t previousNativeSlot = 0;
        ResolvedPosition previousPosition{};
        if (!native_equipment_slot(*equipped, previousNativeSlot)
            || previousNativeSlot != requestedNativeSlot
            || !find_resolved_position(beforeLoadout, equipped->instanceSoid, previousPosition)
            || !previousPosition.equipped
            || previousPosition.equipmentSlot != requestedNativeSlot) {
            return false;
        }
        previousInstanceSoid = equipped->instanceSoid;
        previousDefinitionHash = equipped->definitionHash;
        std::swap(*equipped, after.inventory.values[inventoryIndex]);
    } else {
        equipped = after.inventory.values[inventoryIndex];
        for (std::size_t index = inventoryIndex; index + 1U < after.inventory.count; ++index) {
            after.inventory.values[index] = after.inventory.values[index + 1U];
        }
        --after.inventory.count;
        after.inventory.values[after.inventory.count] = {};
    }

    std::size_t movedItemCount = 0;
    if (!finalize_equipment_transition(account,
                                       characterIndex,
                                       requestedInstanceSoid,
                                       EquipmentMutationKind::equip,
                                       requestedNativeSlot,
                                       beforeLoadout,
                                       after,
                                       movedItemCount)) {
        return false;
    }

    mutation.beforeCharacter = before;
    mutation.afterCharacter = after;
    mutation.characterSoid = before.soid;
    mutation.requestedInstanceSoid = requestedInstanceSoid;
    mutation.previousInstanceSoid = previousInstanceSoid;
    mutation.characterIndex = characterIndex;
    mutation.equipmentSlotIndex = equipmentSlotIndex;
    mutation.inventoryIndex = inventoryIndex;
    mutation.movedItemCount = movedItemCount;
    mutation.nativeEquipmentSlot = requestedNativeSlot;
    mutation.kind = EquipmentMutationKind::equip;
    mutation.prepared = true;
    report_equipment("prepare",
                     "ok",
                     mutation.kind,
                     mutation.characterSoid,
                     mutation.previousInstanceSoid,
                     mutation.requestedInstanceSoid,
                     mutation.equipmentSlotIndex,
                     mutation.inventoryIndex,
                     mutation.nativeEquipmentSlot,
                     mutation.movedItemCount,
                     previousDefinitionHash,
                     requested.definitionHash);
    return true;
}

/** Prepares one checked equipped-to-inventory transition without changing account State. */
bool prepare_equipment_unequip(std::uint64_t requestedInstanceSoid,
                               PendingEquipmentSwap& mutation) noexcept {
    mutation = {};
    if (activity::nightfall::equipment_locked()) return false;
    const AccountState account = account_snapshot();
    if (requestedInstanceSoid == 0 || !account::valid(account)) {
        return false;
    }

    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            characterIndex = index;
            break;
        }
    }
    if (characterIndex == account.characterCount) {
        return false;
    }

    const CharacterState& before = account.characters[characterIndex];
    if (before.inventory.count >= before.inventory.values.size()) {
        return false;
    }
    family4_loadout::ResolvedLoadout beforeLoadout{};
    if (!family4_loadout::resolve(account, characterIndex, beforeLoadout)) {
        return false;
    }

    std::size_t equipmentSlotIndex = before.equipment.slots.size();
    for (std::size_t index = 0; index < before.equipment.slots.size(); ++index) {
        const auto& item = before.equipment.slots[index];
        if (!item.has_value() || item->instanceSoid != requestedInstanceSoid) {
            continue;
        }
        if (equipmentSlotIndex != before.equipment.slots.size()) {
            return false;
        }
        equipmentSlotIndex = index;
    }
    if (equipmentSlotIndex == before.equipment.slots.size()) {
        return false;
    }

    const authored_inventory::Item& requested = *before.equipment.slots[equipmentSlotIndex];
    std::uint8_t requestedNativeSlot = 0;
    std::uint8_t requestedBucketId = 0;
    std::size_t expectedSemanticIndex = authored_inventory::kEquipmentSlotCount;
    ResolvedPosition requestedPosition{};
    if (!native_equipment_slot(requested, requestedNativeSlot)
        || !inventory_bucket_id(requested, requestedBucketId)
        || !semantic_equipment_slot(requestedNativeSlot, expectedSemanticIndex)
        || expectedSemanticIndex != equipmentSlotIndex
        || !find_resolved_position(beforeLoadout, requestedInstanceSoid, requestedPosition)
        || !requestedPosition.equipped || requestedPosition.equipmentSlot != requestedNativeSlot) {
        return false;
    }

    std::size_t inventoryIndex = before.inventory.count;
    for (std::size_t index = 0; index < before.inventory.count; ++index) {
        std::uint8_t inventoryBucketId = 0;
        if (!inventory_bucket_id(before.inventory.values[index], inventoryBucketId)) {
            return false;
        }
        if (inventoryBucketId == requestedBucketId) {
            inventoryIndex = index;
            break;
        }
    }

    CharacterState after = before;
    const authored_inventory::Item unequipped = *after.equipment.slots[equipmentSlotIndex];
    for (std::size_t index = after.inventory.count; index > inventoryIndex; --index) {
        after.inventory.values[index] = after.inventory.values[index - 1U];
    }
    after.inventory.values[inventoryIndex] = unequipped;
    ++after.inventory.count;
    after.equipment.slots[equipmentSlotIndex].reset();

    std::size_t movedItemCount = 0;
    if (!finalize_equipment_transition(account,
                                       characterIndex,
                                       requestedInstanceSoid,
                                       EquipmentMutationKind::unequip,
                                       requestedNativeSlot,
                                       beforeLoadout,
                                       after,
                                       movedItemCount)) {
        return false;
    }

    mutation.beforeCharacter = before;
    mutation.afterCharacter = after;
    mutation.characterSoid = before.soid;
    mutation.requestedInstanceSoid = requestedInstanceSoid;
    mutation.characterIndex = characterIndex;
    mutation.equipmentSlotIndex = equipmentSlotIndex;
    mutation.inventoryIndex = inventoryIndex;
    mutation.movedItemCount = movedItemCount;
    mutation.nativeEquipmentSlot = requestedNativeSlot;
    mutation.kind = EquipmentMutationKind::unequip;
    mutation.prepared = true;
    report_equipment("prepare",
                     "ok",
                     mutation.kind,
                     mutation.characterSoid,
                     mutation.previousInstanceSoid,
                     mutation.requestedInstanceSoid,
                     mutation.equipmentSlotIndex,
                     mutation.inventoryIndex,
                     mutation.nativeEquipmentSlot,
                     mutation.movedItemCount,
                     0,
                     requested.definitionHash);
    return true;
}

/** Commits one prepared equipment after-image behind an exact character staleness guard. */
bool commit_equipment_swap(PendingEquipmentSwap& mutation) noexcept {
    const PendingEquipmentSwap prepared = mutation;
    mutation = {};
    const activity::nightfall::EquipmentMutation nightfallGuard;
    if (!nightfallGuard.allowed()) return false;
    if (!prepared.prepared || prepared.characterSoid == 0 || prepared.requestedInstanceSoid == 0
        || (prepared.kind != EquipmentMutationKind::equip
            && prepared.kind != EquipmentMutationKind::unequip)
        || prepared.characterIndex >= kCharacterCapacity
        || prepared.equipmentSlotIndex >= authored_inventory::kEquipmentSlotCount
        || prepared.inventoryIndex >= authored_inventory::kCharacterItemCapacity
        || prepared.nativeEquipmentSlot >= item_details::kEquipmentSlotCount
        || prepared.beforeCharacter.soid != prepared.characterSoid
        || prepared.afterCharacter.soid != prepared.characterSoid) {
        return false;
    }

    const std::uint32_t previousDefinitionHash =
        character_item_definition_hash(prepared.afterCharacter, prepared.previousInstanceSoid);
    const std::uint32_t requestedDefinitionHash =
        character_item_definition_hash(prepared.afterCharacter, prepared.requestedInstanceSoid);
    report_equipment("commit_begin",
                     "ok",
                     prepared.kind,
                     prepared.characterSoid,
                     prepared.previousInstanceSoid,
                     prepared.requestedInstanceSoid,
                     prepared.equipmentSlotIndex,
                     prepared.inventoryIndex,
                     prepared.nativeEquipmentSlot,
                     prepared.movedItemCount,
                     previousDefinitionHash,
                     requestedDefinitionHash);

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    if (prepared.characterIndex >= candidate.characterCount
        || !same_character(candidate.characters[prepared.characterIndex],
                           prepared.beforeCharacter)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    candidate.characters[prepared.characterIndex] = prepared.afterCharacter;
    family4_loadout::ResolvedLoadout checkedAfter{};
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, prepared.characterIndex, checkedAfter)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    ResolvedPosition requestedPosition{};
    const bool expectedEquipped = prepared.kind == EquipmentMutationKind::equip;
    if (!find_resolved_position(checkedAfter, prepared.requestedInstanceSoid, requestedPosition)
        || requestedPosition.equipmentSlot != prepared.nativeEquipmentSlot
        || requestedPosition.equipped != expectedEquipped) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!persistence::commit_account(runtime::storage::g_state.account, candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);

    report_equipment("commit_end",
                     "ok",
                     prepared.kind,
                     prepared.characterSoid,
                     prepared.previousInstanceSoid,
                     prepared.requestedInstanceSoid,
                     prepared.equipmentSlotIndex,
                     prepared.inventoryIndex,
                     prepared.nativeEquipmentSlot,
                     prepared.movedItemCount,
                     previousDefinitionHash,
                     requestedDefinitionHash);
    return true;
}

/** @return A copy of the active account state, read under the lock. */
AccountState account_snapshot() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const AccountState snapshot = runtime::storage::g_state.account;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return snapshot;
}

/** Reads the account the family-zero banner is built from. */
AccountState banner_account_snapshot() noexcept {
    AccountState snapshot = account_snapshot();
    const AccountState& templates = core::settings::get().characterTemplates;
    if (snapshot.characterCount != 0 || snapshot.primarySoid == 0 || templates.characterCount == 0) {
        return snapshot;
    }
    // The first free key of an empty account is the one the first created character is given.
    CharacterState& standIn = snapshot.characters[0];
    standIn = templates.characters[0];
    standIn.soid = snapshot.primarySoid + 1U;
    standIn.selected = false;
    snapshot.characterCount = 1;
    return snapshot;
}

/** @return How many characters the active account holds, read under the lock without a copy. */
std::size_t account_character_count() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const std::size_t count = runtime::storage::g_state.account.characterCount;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return count;
}

/** Checks the current account snapshot once; the pure predicate validates the character id. */
bool has_current_equipped_festival_mask(std::uint64_t characterId) noexcept {
    const AccountState snapshot = account_snapshot();
    return account::festival_mask::has_current_equipped_festival_mask(characterId, snapshot);
}

} // namespace dawn::state
