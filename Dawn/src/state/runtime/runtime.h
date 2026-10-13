#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../build_data/material_requirements/material_requirement_catalog.h"
#include "state.h"
#include "../activity/Newlight/launchpad/quest_runtime.h"

namespace dawn::state {

/**
 * Assigns runtime SOIDs only to installed profile mod/shader rows which are socket action sources.
 * Currency, material, and consumable profile rows remain canonically non-instanced.
 */
[[nodiscard]] bool ensure_profile_item_identities() noexcept;

/** Direction of one checked character equipment mutation. */
enum class EquipmentMutationKind : std::uint8_t {
    none,
    equip,
    unequip,
};

/** Prepared character-inventory mutation kept private until its response and update both fit. */
struct PendingEquipmentSwap {
    /** Exact prepare-time character view used as the commit staleness guard. */
    CharacterState beforeCharacter{};
    /** Canonical after-image, including every row-change mutation generation. */
    CharacterState afterCharacter{};
    std::uint64_t characterSoid{};
    std::uint64_t requestedInstanceSoid{};
    std::uint64_t previousInstanceSoid{};
    std::size_t characterIndex{};
    std::size_t equipmentSlotIndex{};
    std::size_t inventoryIndex{};
    std::size_t movedItemCount{};
    std::uint8_t nativeEquipmentSlot{};
    EquipmentMutationKind kind{};
    bool prepared{};
};

enum class AcquisitionSource : std::uint8_t { collections, missionReward, vendorReward };

/** Prepared selected-character inventory insertion kept private until its reply and push fit. */
struct PendingItemAcquisition {
    AcquisitionSource source{AcquisitionSource::collections};
    std::uint16_t rewardGlimmer{};
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    /** Exact profile material view observed before and after charging the native requirement set.
     */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t acquiredInstanceSoid{};
    std::uint64_t removedInstanceSoid{};
    std::uint32_t acquiredDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::uint32_t expectedNextInventorySerial{};
    std::size_t characterIndex{};
    std::size_t expectedInventoryCount{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    std::size_t inventoryIndex{};
    std::uint16_t collectibleIndex{};
    std::uint16_t inventoryRow{};
    std::uint8_t equipmentSlot{};
    std::uint8_t materialRequirementCount{};
    bool profileChanged{};
    /** Optional one-item replacement staged on this acquisition. Zero means no update. */
    std::uint64_t updatedInstanceSoid{};
    /** Authored dense inventory index of the pre-existing item being replaced. */
    std::size_t updatedInventoryIndex{};
    /** Native row/slot of the updated item in the canonical after-image. */
    std::uint16_t updatedInventoryRow{};
    std::uint8_t updatedEquipmentSlot{};
    /** Before/after installed definition identities and item-row mutation serials. */
    std::uint16_t updatedBeforeDefinitionIndex{};
    std::uint16_t updatedAfterDefinitionIndex{};
    std::uint32_t updatedBeforeDefinitionHash{};
    std::uint32_t updatedAfterDefinitionHash{};
    std::int32_t updatedBeforeMutationSerial{};
    std::int32_t updatedAfterMutationSerial{};
    bool prepared{};
};

/** One profile row an exchange changed, named by its account change-ring serial. */
struct ProfileStackChange {
    std::int32_t mutationSerial{};
    std::int32_t afterQuantity{};
};

/** Maximum number of profile rows one vendor exchange may announce. */
inline constexpr std::size_t kProfileStackChangeCapacity = 4;

/** Prepared account-profile stack insertion kept private until its reply and account upsert fit. */
struct PendingProfileItemAcquisition {
    /** Exact profile inventory observed while preparing the mutation. */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeItems{};
    /** Canonical profile inventory after incrementing or appending one stack. */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterItems{};
    std::uint64_t accountSoid{};
    /** Stable profile-row source identity, preserved for increments and allocated for appends. */
    std::uint64_t acquiredInstanceSoid{};
    std::uint32_t acquiredDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::size_t expectedItemCount{};
    std::size_t afterItemCount{};
    std::size_t profileIndex{};
    std::int32_t previousQuantity{};
    std::int32_t acquiredQuantity{};
    std::int32_t grantQuantity{1};
    std::int32_t previousMutationSerial{};
    std::int32_t acquiredMutationSerial{};
    std::uint16_t collectibleIndex{};
    std::uint8_t bucketId{};
    std::uint8_t materialRequirementCount{};
    /** Non-empty only for a vendor exchange that credits these rows. */
    std::array<ProfileStackChange, kProfileStackChangeCapacity> changes{};
    std::size_t changeCount{};
    /** True only for installed profile mod/shader rows materialized as Family-4 residents. */
    bool actionSource{};
    bool appended{};
    /** True for a server-authored free currency credit rather than a Collections purchase. */
    bool rewardGrant{};
    bool prepared{};
};

/** Result of preparing a bounded server-authored profile currency credit. */
enum class ProfileCurrencyGrantResult : std::uint8_t {
    prepared,
    capped,
    rejected,
};

/** @return Positive credited headroom, or zero for invalid/capped inputs. */
[[nodiscard]] constexpr std::int32_t profile_currency_credit(
    std::int32_t previous,
    std::int32_t maximum,
    std::int32_t requested) noexcept {
    return previous < 0 || maximum <= 0 || previous >= maximum || requested <= 0
               ? 0
               : (std::min)(requested, maximum - previous);
}

/** @return True when a reward after-image changes only its one credited profile row. */
[[nodiscard]] constexpr bool profile_currency_grant_after_image_exact(
    const PendingProfileItemAcquisition& mutation) noexcept {
    if (!mutation.rewardGrant
        || (mutation.appended
            && (mutation.afterItemCount != mutation.expectedItemCount + 1U
                || mutation.profileIndex != mutation.expectedItemCount))
        || (!mutation.appended && mutation.afterItemCount != mutation.expectedItemCount)) {
        return false;
    }
    for (std::size_t index = 0; index < mutation.afterItems.size(); ++index) {
        if (index == mutation.profileIndex) continue;
        const auto& before = mutation.beforeItems[index];
        const auto& after = mutation.afterItems[index];
        if (before.instanceSoid != after.instanceSoid
            || before.definitionHash != after.definitionHash || before.quantity != after.quantity
            || before.mutationSerial != after.mutationSerial) {
            return false;
        }
    }
    return true;
}

/** One profile material actually credited by a prepared dismantle. */
struct DismantleReward {
    std::uint32_t definitionHash{};
    std::size_t profileIndex{};
    std::int32_t quantity{};
    std::int32_t afterQuantity{};
    std::int32_t mutationSerial{};
};

/** Dismantle feedback can publish every bounded server-authored policy row. */
inline constexpr std::size_t kDismantleRewardCapacity = kDismantleRewardPolicyCapacity;

/** Prepared selected-character inventory removal kept private until its reply and push fit. */
struct PendingItemDismantle {
    /** Exact prepare-time character view used as the commit staleness guard. */
    CharacterState beforeCharacter{};
    /** Canonical dense inventory after-image; array compaction shifts rows without renumbering. */
    CharacterState afterCharacter{};
    /** Exact profile material view observed before and after applying the dismantle payout. */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::array<DismantleReward, kDismantleRewardCapacity> rewards{};
    account::inventory::Item dismantledItem{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t dismantledInstanceSoid{};
    std::size_t characterIndex{};
    std::size_t expectedInventoryCount{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    std::size_t inventoryIndex{};
    std::size_t movedInventoryItemCount{};
    std::size_t rewardCount{};
    std::uint16_t inventoryRow{};
    std::uint8_t equipmentSlot{};
    bool profileChanged{};
    bool prepared{};
};

/** Prepared ordinary-socket selection for one selected-character item instance. */
struct PendingSocketPlug {
    /** Exact prepare-time character view used as the commit staleness guard. */
    CharacterState beforeCharacter{};
    /** Canonical after-image. Only the target item's authored socket block differs. */
    CharacterState afterCharacter{};
    /** Exact account-wide material balances observed before applying the installed cost set. */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        beforeProfileItems{};
    /** Canonical material balances after every consuming row in the installed cost set. */
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        afterProfileItems{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t targetInstanceSoid{};
    std::uint32_t targetDefinitionHash{};
    std::uint32_t plugDefinitionHash{};
    std::uint32_t materialRequirementSetHash{};
    std::size_t characterIndex{};
    std::size_t expectedProfileItemCount{};
    std::size_t afterProfileItemCount{};
    /** Equipment semantic index or dense inventory index, selected by `targetEquipped`. */
    std::size_t itemIndex{};
    std::uint16_t targetDefinitionIndex{};
    std::uint16_t plugDefinitionIndex{};
    std::uint16_t materialRequirementSetIndex{0xFFFFU};
    std::uint8_t socketLane{};
    std::uint8_t targetBucketId{};
    std::uint8_t plugBucketId{};
    std::uint8_t materialRequirementCount{};
    bool profileChanged{};
    bool targetEquipped{};
    bool prepared{};
};

/** Prepared accumulated item-state change for one selected-character item instance. */
struct PendingItemState {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    std::uint64_t characterSoid{};
    std::uint64_t targetInstanceSoid{};
    std::size_t characterIndex{};
    /** Equipment semantic index or dense inventory index, selected by `targetEquipped`. */
    std::size_t itemIndex{};
    std::uint16_t targetDefinitionIndex{};
    std::uint32_t beforeFlags{};
    std::uint32_t afterFlags{};
    bool targetEquipped{};
    bool prepared{};
};

/**
 * Loads cached build data and generates secrets with Dawn's authored activity defaults.
 * @param module Loaded Dawn module, or null to disable disk persistence.
 * @param initialAccount Empty State, or a complete checked account from Core settings.
 * @return True when the cached data passes its checks and every secret is generated.
 */
[[nodiscard]] bool initialize(void* module = nullptr,
                              const AccountState& initialAccount = {}) noexcept;

/**
 * Loads cached build data and publishes fixed activity defaults in one step.
 * @param module Loaded Dawn module, or null to disable disk persistence.
 * @param initialAccount Empty State, or a complete checked account from Core settings.
 * @param activityDefaults Complete local fallback policy from immutable Core settings.
 * @return True when account, defaults, cached data, and generated secrets are valid.
 */
[[nodiscard]] bool
initialize(void* module,
           const AccountState& initialAccount,
           const activity::defaults::ActivityDefaults& activityDefaults) noexcept;

/** Securely clears State, including activity destinations and matchmaking descriptors. */
void shutdown() noexcept;

/** @return Immutable generated SignOn session fields. */
[[nodiscard]] const SignOnState& sign_on() noexcept;

[[nodiscard]] bool publish_bootstrap_token(std::span<const std::byte> token) noexcept;

/** @return Immutable generated BAP session fields. */
[[nodiscard]] const BapState& bap() noexcept;

/**
 * Stores the active nonzero account key when the account remains complete.
 * @param primarySoid Account key selected by the local Client.
 * @return False when the key or resulting account State is invalid.
 */
[[nodiscard]] bool set_primary_soid(std::uint64_t primarySoid) noexcept;

/**
 * Moves the selection to one authored character.
 * The Client names its pick only in the select-character request, so this is where a player's
 * choice enters State.
 * @param characterSoid Picked character key, which must name an authored character.
 * @param changed Receives whether the selection moved to a different character.
 * @return False when no authored character carries that key.
 */
[[nodiscard]] bool set_selected_character(std::uint64_t characterSoid, bool& changed) noexcept;

/**
 * Adds one character to the account from the authored template of the requested class.
 * The character takes the requested identity and its own item instances, and is stored before this
 * returns, so the next roster the Client asks for already lists it. It becomes the selected
 * character, and any earlier selection is released.
 * @param race Race the player chose.
 * @param gender Gender the player chose.
 * @param characterClass Class the player chose, which picks the template.
 * @param characterSoid Receives the new character's key.
 * @return False when the account has no free slot, the account key is not set, no template of
 * that class is authored, or the resulting account does not validate. State is unchanged then.
 */
[[nodiscard]] bool create_character(CharacterRace race,
                                    CharacterGender gender,
                                    CharacterClass characterClass,
                                    std::uint64_t& characterSoid) noexcept;

/**
 * Removes one character from the account, with everything the account stored for it.
 * The other characters keep their keys and their order, so the Client's view of them stays valid.
 * The removed character's items, missions, progress and unlocks go with it. If it was the selected
 * character the account is left with no selection, as it is at character select.
 * @param characterSoid Key of the character the player deleted.
 * @return False when the account has no such character or the result cannot be stored. State is
 * unchanged then.
 */
[[nodiscard]] bool delete_character(std::uint64_t characterSoid) noexcept;

/**
 * Prepares an equip operation for one unequipped instance on the selected character.
 * An occupied slot is swapped; an empty semantic slot receives the requested item directly.
 *
 * @param requestedInstanceSoid Unequipped item instance selected by the Client.
 * @param mutation
 * Gets the checked after-image without changing account State.
 * @return True when the instance is
 * owned, unequipped, and maps to one supported native equipment slot.
 */
[[nodiscard]] bool prepare_equipment_swap(std::uint64_t requestedInstanceSoid,
                                          PendingEquipmentSwap& mutation) noexcept;

/**
 * Prepares an unequip operation for one equipped selected-character instance.
 * The item is inserted before existing inventory items in its native bucket so their published
 * rows remain stable. Native slots without a proven semantic State mapping are rejected.
 *
 * @param requestedInstanceSoid Equipped item instance selected by the Client.
 * @param mutation Gets the checked after-image without changing account State.
 * @return True when the instance is equipped and the dense character inventory has room.
 */
[[nodiscard]] bool prepare_equipment_unequip(std::uint64_t requestedInstanceSoid,
                                             PendingEquipmentSwap& mutation) noexcept;

/**
 * Commits a prepared equipment mutation only while the full captured character still matches.
 *
 * @param mutation Prepared mutation, always cleared before this function returns.
 * @return True
 * when the equip or unequip commits atomically and leaves the whole account valid.
 */
[[nodiscard]] bool commit_equipment_swap(PendingEquipmentSwap& mutation) noexcept;

/** Acquisition policy: actual drops roll eligible weapons; Collections keep authored defaults. */
struct ItemAcquisitionOptions {
    bool allowRandomRoll{true};
    std::uint64_t seed{};
};

/** Prepares a direct item grant without a Collections price or entitlement. */
[[nodiscard]] bool prepare_item_acquisition_for_item(
    std::uint16_t itemDefinitionIndex,
    PendingItemAcquisition& mutation,
    ItemAcquisitionOptions options = {},
    std::span<const build_data::material_requirements::Requirement> cost = {}) noexcept;

/**
 * Prepares one installed equippable definition as a new selected-character inventory instance.
 *
 * Native-default sockets, a unique runtime SOID, and the selected character's current item level
 * are used. Full loadout resolution is the authoritative bucket-capacity check.
 *
 * An authored cost REPLACES the collectible's own installed material set rather than adding to
 * it: the collectible's set is the Collections re-pull price, which has nothing to do with what a
 * vendor row charges. It rides purely as the before/after profile delta - `materialRequirementSetHash`
 * and `materialRequirementCount` keep describing only the collectible, because that pair is what
 * `commit_item_acquisition` compares against the installed collectible.
 *
 * @param collectibleIndex Collections row the Client pulled from.
 * @param definitionHash Installed item definition requested by the Client.
 * @param mutation Gets a checked after-image without changing account State.
 * @param cost Explicit vendor price; empty uses Collections cost only when collectibleIndex is set.
 * @param options Roll policy for direct rewards; ignored for Collections reclaims.
 * @return True when the item and every existing loadout row resolve with one free native row.
 */
[[nodiscard]] bool prepare_item_acquisition(
    std::uint16_t collectibleIndex,
    std::uint32_t definitionHash,
    PendingItemAcquisition& mutation,
    std::span<const build_data::material_requirements::Requirement> cost = {},
    ItemAcquisitionOptions options = {}) noexcept;

/** Mission reward overload; preserves atomic quest reward and Glimmer credit. */
[[nodiscard]] bool prepare_item_acquisition(std::uint16_t collectibleIndex,
    std::uint32_t definitionHash, PendingItemAcquisition& mutation,
    AcquisitionSource source, std::uint16_t rewardGlimmer = 0) noexcept;

/** Builds the exact full-account after-image while a prepared item pull remains current. */
[[nodiscard]] bool preview_item_acquisition(const PendingItemAcquisition& mutation,
                                            AccountState& after) noexcept;

/**
 * Commits a prepared inventory insertion only while its selected character, existing loadout,
 * and next inventory serial still match the prepare-time view.
 *
 * @param mutation Prepared mutation, always cleared before this function returns.
 * @return True when the insertion commits atomically and leaves the whole account valid.
 */
[[nodiscard]] bool commit_item_acquisition(PendingItemAcquisition& mutation) noexcept;

/** Stages one pre-existing unequipped item replacement on an already prepared acquisition. */
[[nodiscard]] bool stage_item_replacement(PendingItemAcquisition& mutation,
                                           std::uint64_t instanceSoid,
                                           std::size_t inventoryIndex,
                                           std::uint32_t beforeDefinitionHash,
                                           std::uint32_t afterDefinitionHash) noexcept;

/**
 * Prepares one installed profile-owned stackable definition for a Collections pull.
 *
 * An existing non-full stack is incremented. Otherwise a new dense State entry is appended only
 * when the installed profile bucket still owns a free native row.
 *
 * An authored cost REPLACES the collectible's own installed material set, exactly as it does on
 * the character path, and reaches State only as the before/after profile delta.
 *
 * @param collectibleIndex Collections row the Client pulled from.
 * @param definitionHash Installed stackable definition requested by the Client.
 * @param mutation Gets the checked profile before/after images without changing account State.
 * @param cost Authored cost charged inside this same transaction; empty charges nothing.
 * @return True when the definition belongs to the main profile array and one unit fits.
 */
[[nodiscard]] bool prepare_profile_item_acquisition(
    std::uint16_t collectibleIndex,
    std::uint32_t definitionHash,
    PendingProfileItemAcquisition& mutation,
    std::span<const build_data::material_requirements::Requirement> cost = {}, std::int32_t quantity = 1) noexcept;

/**
 * Materializes a prepared profile acquisition over the current account only while its complete
 * profile-inventory view is unchanged. This is the account object encoded before commit.
 *
 * @param mutation Prepared mutation that remains owned by the transaction.
 * @param after Gets the exact full-account after-image used by the Family-4 upsert.
 * @return True when the mutation is whole and its prepare-time profile remains current.
 */
[[nodiscard]] bool preview_profile_item_acquisition(const PendingProfileItemAcquisition& mutation,
                                                    AccountState& after) noexcept;

/**
 * Commits a prepared profile stack insertion only while its prepare-time profile remains current.
 *
 * @param mutation Prepared mutation, always cleared before this function returns.
 * @return True when the stack update commits atomically and leaves the whole account valid.
 */
[[nodiscard]] bool
commit_profile_item_acquisition(PendingProfileItemAcquisition& mutation) noexcept;

/** Commits a prepared completion-reward credit and resolves its durable debt atomically. */
[[nodiscard]] bool commit_profile_item_reward(PendingProfileItemAcquisition& mutation,
                                              std::uint64_t debtId,
                                              std::int32_t credited) noexcept;

/**
 * Prepares one free credit into the definition's sole profile-bucket stack.
 * Existing quantity is saturated at the installed max; a capped stack is a successful no-op.
 */
[[nodiscard]] ProfileCurrencyGrantResult
prepare_profile_currency_grant(std::uint32_t definitionHash,
                               std::int32_t quantity,
                               PendingProfileItemAcquisition& mutation) noexcept;

/** One credited side of a vendor exchange. */
struct ProfileExchangePayout {
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
};

/** Stages package materials on an existing gear grant; its charge and rewards commit together. */
[[nodiscard]] bool stage_item_profile_rewards(PendingItemAcquisition& mutation,
    std::span<const ProfileExchangePayout> payouts) noexcept;

/** Prepares an atomic profile-stack charge and one or more credited payouts. */
[[nodiscard]] bool prepare_vendor_exchange(std::uint32_t costDefinitionHash,
                                           std::int32_t costQuantity,
                                           std::span<const ProfileExchangePayout> payouts,
                                           PendingProfileItemAcquisition& mutation) noexcept;

/**
 * Prepares removal of one unequipped instance from the selected character.
 *
 * The authored inventory prefix is compacted. Surviving items retain their mutation generations
 * while their native rows shift. Equipped items are never accepted.
 *
 * @param instanceSoid Unequipped item-instance key selected by the Client.
 * @param mutation Gets checked before/after images without changing account State.
 * @return True when the instance is uniquely owned by the selected character and both loadouts
 * resolve completely.
 */
[[nodiscard]] bool prepare_item_dismantle(std::uint64_t instanceSoid,
                                          PendingItemDismantle& mutation) noexcept;

/** Builds the exact account after-image while a prepared dismantle remains current. */
[[nodiscard]] bool preview_item_dismantle(const PendingItemDismantle& mutation,
                                          AccountState& after) noexcept;

/**
 * Commits a prepared inventory removal only while the complete prepare-time character view is
 * unchanged.
 *
 * @param mutation Prepared mutation, always cleared before this function returns.
 * @return True when the removal commits atomically and leaves the whole account valid.
 */
[[nodiscard]] bool commit_item_dismantle(PendingItemDismantle& mutation) noexcept;

/**
 * Prepares one exact opcode-903 ordinary-socket selection on a selected-character item.
 *
 * The target may be equipped or unequipped. Native defaults are first materialized into a complete
 * authored socket block, then only the requested lane changes. Item identity, native row,
 * quantity, level, and mutation generation remain byte-for-byte stable.
 *
 * @param targetInstanceSoid Selected-character item-instance key named by the Client.
 * @param socketLane Zero-based ordinary socket lane.
 * @param plugDefinitionIndex Installed plug-definition row selected by the Client.
 * @param mutation Gets the checked before/after images without changing account State.
 * @return True when ownership, item detail, lane, plug compatibility, and both loadouts validate.
 */
[[nodiscard]] bool prepare_socket_plug(std::uint64_t targetInstanceSoid,
                                       std::uint8_t socketLane,
                                       std::uint16_t plugDefinitionIndex,
                                       PendingSocketPlug& mutation) noexcept;

/**
 * Prepares one ordinary-socket selection for an exact character-screen item selector.
 *
 * The resolved selected-character instance is passed through the same checked transition as an
 * instance-addressed action, so acquired and unequipped items do not depend on a coincidental
 * menu-row ordinal.
 *
 * @param instanceIdentityToken Item-instance identity decoded from the opcode-1901 selector.
 * @param requestedSocketLane Native socket action lane; the installed compatibility relation
 * resolves the target's exact physical lane.
 * @param plugDefinitionIndex Installed plug-definition row selected by the Client.
 * @param mutation Gets the checked before/after images without changing account State.
 * @return True when the location has one matching item, the plug resolves to exactly the
 * requested compatible ordinary socket lane, and the socket transition is valid.
 */
[[nodiscard]] bool prepare_character_selector_socket_plug(std::uint64_t instanceIdentityToken,
                                                          std::uint8_t requestedSocketLane,
                                                          std::uint16_t plugDefinitionIndex,
                                                          PendingSocketPlug& mutation) noexcept;

/** Produces the complete uncommitted account after-image for a prepared socket transaction. */
[[nodiscard]] bool preview_socket_plug(const PendingSocketPlug& mutation,
                                       AccountState& after) noexcept;

/**
 * Commits a prepared socket selection only while the complete prepare-time character is unchanged.
 * @param mutation Prepared mutation, always cleared before this function returns.
 * @return True when the exact canonical transition commits atomically.
 */
[[nodiscard]] bool commit_socket_plug(PendingSocketPlug& mutation) noexcept;

/** Prepares one complete native item-state value for an owned selected-character instance. */
[[nodiscard]] bool prepare_item_state(std::uint64_t targetInstanceSoid,
                                      std::uint16_t targetDefinitionIndex,
                                      std::uint32_t flags,
                                      PendingItemState& mutation) noexcept;

/** Commits one prepared item-state change behind an exact full-character staleness guard. */
[[nodiscard]] bool commit_item_state(PendingItemState& mutation) noexcept;

/** @return A copy of the active account state, read under the lock. */
[[nodiscard]] AccountState account_snapshot() noexcept;

/**
 * The account as the family-zero banner is built from it.
 * The Client's banner record takes its snapshot only in the short window its subscribe opens, so an
 * account with no character is shown with one stand-in: the first authored template, under the key
 * the first created character takes. Creating that character refreshes the same record in place.
 * Without a template the account is returned as it is.
 * @return A copy of the active account state, read under the lock.
 */
[[nodiscard]] AccountState banner_account_snapshot() noexcept;

/** @return How many characters the active account holds, read under the lock without a copy. */
[[nodiscard]] std::size_t account_character_count() noexcept;

/** @return True when the named existing character currently equips an allowed Festival mask. */
[[nodiscard]] bool has_current_equipped_festival_mask(std::uint64_t characterId) noexcept;

/** @return A copy of the evaluated content state, read under the lock. */
[[nodiscard]] InvestmentState investment_snapshot() noexcept;

} // namespace dawn::state
