#include "web_service_actions.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "../../core/settings/rule_text.h"
#include "../../middleware/web_service/messages/opcode1820.h"
#include "../../middleware/web_service/messages/opcode1901.h"
#include "../../middleware/web_service/messages/opcode402.h"
#include "../../middleware/web_service/messages/opcode403.h"
#include "../../middleware/web_service/messages/opcode406.h"
#include "../../middleware/web_service/messages/opcode502.h"
#include "../../middleware/web_service/messages/opcode504.h"
#include "../../middleware/web_service/messages/opcode903.h"
#include "../../middleware/web_service/messages/opcode901/opcode901_codec.h"
#include "../../middleware/web_service/messages/opcode904/opcode904_codec.h"
#include "../../middleware/web_service/messages/opcode601/opcode601_codec.h"
#include "../../state/account/festival_mask.h"
#include "../../state/activity/destination/activity_destination_snapshot.h"
#include "../../state/activity/runtime.h"
#include "../../state/account/account_state.h"
#include "../../state/account/festival_quest.h"
#include "../../state/account/pursuit_hold.h"
#include "../../state/activity/events/activity_event_selection.h"
#include "../../state/build_data/items/item_catalog.h"
#include "../../state/build_data/runtime.h"
#include "../../state/build_data/vendors/vendor_catalog.h"
#include "../../state/runtime/runtime.h"
#include "../../state/vendors/answered_interactions.h"
#include "forest_loot_pickups.h"
#include "festival_grab_bags.h"

namespace dawn::server::web_service {

namespace {

/** Socket kind the shader model occupies, which is the only kind a shader swap may target. */
constexpr std::uint8_t kEquippedShaderModelSocketKind = 0;
/** Index stored when no definition resolves. The catalog is u16-indexed, so this cannot be one. */
constexpr std::uint32_t kUnavailableDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();
constexpr std::uint32_t kRepeatableHoldLimit = 5;
constexpr std::size_t kRepeatablePoolCapacity = 64;
constexpr std::size_t kExchangePayoutCapacity = 4;
static_assert(kExchangePayoutCapacity <= state::kProfileStackChangeCapacity);
std::array<char, core::rule_text::kRuleTextCapacity> g_ruleText{};

} // namespace

/** Logs one exact correlated equipment response after its Queuez update is staged. */
void report_equip_response(const middleware::web_service::Message& message,
                           std::int32_t family4Version,
                           std::span<const std::byte> response) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(line.data(),
                                     line.size(),
                                     "ev=equipment stage=response opcode=%u transaction=%u "
                                     "family_version=%d bytes=%zu hex=",
                                     static_cast<unsigned>(message.opcode),
                                     static_cast<unsigned>(message.transactionId),
                                     family4Version,
                                     response.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** Logs the final item-creation status pair and the exact Family-4 revision it promises. */
void report_item_acquisition_response(const middleware::web_service::Message& message,
                                      std::int32_t family4Version,
                                      std::uint64_t acquiredInstanceSoid,
                                      std::span<const std::byte> response) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(
        line.data(),
        line.size(),
        "ev=acquire stage=response result=ok opcode=%u transaction=%u family_version=%d "
        "instance=0x%llX bytes=%zu hex=",
        static_cast<unsigned>(message.opcode),
        static_cast<unsigned>(message.transactionId),
        family4Version,
        static_cast<unsigned long long>(acquiredInstanceSoid),
        response.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** Logs the final profile-stack status pair and the exact Family-4 account revision it promises. */
void report_profile_item_acquisition_response(const middleware::web_service::Message& message,
                                              std::int32_t family4Version,
                                              std::uint32_t definitionHash,
                                              std::int32_t quantity,
                                              std::span<const std::byte> response) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=profile_acquire stage=response result=ok opcode=%u transaction=%u "
                      "family_version=%d definition_hash=0x%08X quantity=%d bytes=%zu hex=",
                      static_cast<unsigned>(message.opcode),
                      static_cast<unsigned>(message.transactionId),
                      family4Version,
                      definitionHash,
                      quantity,
                      response.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** Logs the final dismantle status pair and the exact Family-4 revision it promises. */
void report_item_dismantle_response(const middleware::web_service::Message& message,
                                    std::int32_t family4Version,
                                    std::uint64_t dismantledInstanceSoid,
                                    std::span<const std::byte> response) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(
        line.data(),
        line.size(),
        "ev=dismantle stage=response result=ok opcode=%u transaction=%u family_version=%d "
        "instance=0x%llX bytes=%zu hex=",
        static_cast<unsigned>(message.opcode),
        static_cast<unsigned>(message.transactionId),
        family4Version,
        static_cast<unsigned long long>(dismantledInstanceSoid),
        response.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** Logs the exact opcode-903 status pair and the item-instance revision it promises. */
void report_socket_plug_response(const middleware::web_service::Message& message,
                                 std::int32_t family4Version,
                                 std::uint64_t targetInstanceSoid,
                                 std::uint8_t socketLane,
                                 std::uint16_t plugDefinitionIndex,
                                 std::span<const std::byte> response) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(
        line.data(),
        line.size(),
        "ev=socket_plug stage=response result=ok opcode=%u transaction=%u family_version=%d "
        "instance=0x%llX lane=%u plug_definition=%u bytes=%zu hex=",
        static_cast<unsigned>(message.opcode),
        static_cast<unsigned>(message.transactionId),
        family4Version,
        static_cast<unsigned long long>(targetInstanceSoid),
        static_cast<unsigned>(socketLane),
        static_cast<unsigned>(plugDefinitionIndex),
        response.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** One line carries the picked id and whether the selection moved. */
constexpr std::size_t kSelectLineCapacity = 96;

/**
 * Records the player's character pick, which arrives nowhere else.
 * A bad or unknown id leaves the selection alone. The reply is the status pair either way. The
 * Family-4 object move follows this call, and the family-zero pair after it.
 * @param message Parsed select-character request.
 * @param outcome Gets the picked key once the selection has moved in State.
 */
void select_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode504::Request picked;
    if (!middleware::web_service::messages::opcode504::parse_request(message, picked)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws504 stage=parse result=fail");
        return;
    }
    bool changed = false;
    if (!state::set_selected_character(picked.characterSoid, changed)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws504 stage=select result=unknown");
        return;
    }
    outcome.hasSelectedCharacter = true;
    outcome.selectedCharacterSoid = picked.characterSoid;

    std::array<char, kSelectLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=ws504 stage=select result=ok soid=0x%llX changed=%u",
                                      static_cast<unsigned long long>(picked.characterSoid),
                                      static_cast<unsigned>(changed));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Removes the character the player confirmed deleting.
 * An unknown id, or a body that does not parse, leaves the account alone and the reply reports the
 * refusal. State logs why it refused. Once the character is gone the roster and the account graph
 * the Client holds are stale, so the outcome asks for a full refresh of both.
 * @param message Parsed delete-character request.
 * @param outcome Flags the roster change once the character is removed from State.
 */
void delete_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode502::Request doomed;
    if (!middleware::web_service::messages::opcode502::parse_request(message, doomed)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws502 stage=parse result=fail");
        return;
    }
    outcome.rosterChanged = state::delete_character(doomed.characterSoid);
}

/** Reads the shared opcode-403/404 SOID descriptor through its codec. */
[[nodiscard]] bool parse_equipment_instance(const middleware::web_service::Message& message,
                                            std::uint64_t& instanceSoid) noexcept {
    middleware::web_service::messages::opcode403::Request request{};
    const bool parsed =
        middleware::web_service::messages::opcode403::parse_request(message, request);
    instanceSoid = request.instanceSoid;
    return parsed;
}

/** Prepares one opcode-403/404 equipment mutation without publishing State early. */
void mutate_equipment(const middleware::web_service::Message& message,
                      bool unequip,
                      Outcome& outcome) noexcept {
    std::uint64_t requestedInstanceSoid = 0;
    if (!parse_equipment_instance(message, requestedInstanceSoid)) {
        std::array<char, 112> line{};
        const int count = std::snprintf(line.data(),
                                        line.size(),
                                        "ev=equipment stage=parse result=fail opcode=%u "
                                        "payload_bytes=%zu",
                                        static_cast<unsigned>(message.opcode),
                                        message.payload.size());
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    state::PendingEquipmentSwap mutation;
    const bool prepared = unequip
                              ? state::prepare_equipment_unequip(requestedInstanceSoid, mutation)
                              : state::prepare_equipment_swap(requestedInstanceSoid, mutation);
    if (!prepared) {
        std::array<char, 144> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=equipment stage=prepare result=fail opcode=%u action=%s requested=0x%llX",
            static_cast<unsigned>(message.opcode),
            unequip ? "unequip" : "equip",
            static_cast<unsigned long long>(requestedInstanceSoid));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }
    outcome.mutation = mutation;

    std::array<char, 224> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=equipment stage=prepare result=ok opcode=%u action=%s character=0x%llX "
                      "previous=0x%llX requested=0x%llX native_slot=%u moved_items=%zu",
                      static_cast<unsigned>(message.opcode),
                      unequip ? "unequip" : "equip",
                      static_cast<unsigned long long>(mutation.characterSoid),
                      static_cast<unsigned long long>(mutation.previousInstanceSoid),
                      static_cast<unsigned long long>(mutation.requestedInstanceSoid),
                      static_cast<unsigned>(mutation.nativeEquipmentSlot),
                      mutation.movedItemCount);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Parses and prepares one exact selected-character opcode-903 socket selection. */
void mutate_socket_plug(const middleware::web_service::Message& message,
                        Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode903::Request request{};
    if (!middleware::web_service::messages::opcode903::parse_request(message, request)
        || !request.hasInstance || request.instanceSoid == 0 || request.hasTargetDefinition
        || !request.hasPlugDefinition
        || request.socketIndex >= state::account::inventory::kPlugCapacity) {
        std::array<char, 192> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws903 stage=parse result=fail transaction=%u payload_bytes=%zu has_instance=%u "
            "instance=0x%llX has_target_definition=%u socket=%u has_plug_definition=%u",
            static_cast<unsigned>(message.transactionId),
            message.payload.size(),
            static_cast<unsigned>(request.hasInstance),
            static_cast<unsigned long long>(request.instanceSoid),
            static_cast<unsigned>(request.hasTargetDefinition),
            request.socketIndex,
            static_cast<unsigned>(request.hasPlugDefinition));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    state::PendingSocketPlug mutation{};
    if (!state::prepare_socket_plug(request.instanceSoid,
                                    static_cast<std::uint8_t>(request.socketIndex),
                                    request.plugDefinitionIndex,
                                    mutation)) {
        std::array<char, 192> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws903 stage=prepare result=fail transaction=%u instance=0x%llX lane=%u "
            "plug_definition=%u",
            static_cast<unsigned>(message.transactionId),
            static_cast<unsigned long long>(request.instanceSoid),
            request.socketIndex,
            static_cast<unsigned>(request.plugDefinitionIndex));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    outcome.mutation = mutation;
    std::array<char, 240> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws903 stage=prepare result=ok transaction=%u character=0x%llX instance=0x%llX "
        "target_definition=%u target_bucket=%u lane=%u plug_definition=%u plug_bucket=%u "
        "equipped=%u item_index=%zu",
        static_cast<unsigned>(message.transactionId),
        static_cast<unsigned long long>(mutation.characterSoid),
        static_cast<unsigned long long>(mutation.targetInstanceSoid),
        static_cast<unsigned>(mutation.targetDefinitionIndex),
        static_cast<unsigned>(mutation.targetBucketId),
        static_cast<unsigned>(mutation.socketLane),
        static_cast<unsigned>(mutation.plugDefinitionIndex),
        static_cast<unsigned>(mutation.plugBucketId),
        static_cast<unsigned>(mutation.targetEquipped),
        mutation.itemIndex);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Parses and prepares one character-location opcode-1901 socket selection. */
void mutate_equipped_socket_plug(const middleware::web_service::Message& message,
                                 Outcome& outcome) noexcept {
    namespace opcode1901 = middleware::web_service::messages::opcode1901;
    opcode1901::Request request{};
    const bool parsed = opcode1901::parse_request(message, request);
    // A request prepares at most one State mutation, so a run naming several sockets cannot be
    // applied as the one transaction it has to be. It is understood and declined rather than
    // treated as malformed, and the reply now carries that refusal.
    const opcode1901::Replacement& replacement = request.replacements.front();
    if (!parsed || request.replacementCount != 1
        || replacement.modelSocketKind != kEquippedShaderModelSocketKind
        || replacement.auxiliary != 0
        || replacement.socketIndex >= state::account::inventory::kPlugCapacity
        || request.instanceIdentityToken == 0) {
        std::array<char, 256> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws1901 stage=parse result=fail transaction=%u payload_bytes=%zu replacements=%zu "
            "plug_definition=%u canonical_kind=%u model_kind=%u socket=%u auxiliary=0x%llX "
            "equipment_selector=%llu",
            static_cast<unsigned>(message.transactionId),
            message.payload.size(),
            request.replacementCount,
            static_cast<unsigned>(replacement.plugDefinitionIndex),
            static_cast<unsigned>(replacement.canonicalSocketKind),
            static_cast<unsigned>(replacement.modelSocketKind),
            replacement.socketIndex,
            static_cast<unsigned long long>(replacement.auxiliary),
            static_cast<unsigned long long>(request.equipmentSelector));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    const std::uint64_t identityToken = request.instanceIdentityToken;
    state::PendingSocketPlug mutation{};
    if (!state::prepare_character_selector_socket_plug(
            request.instanceIdentityToken,
            static_cast<std::uint8_t>(replacement.socketIndex),
            replacement.plugDefinitionIndex,
            mutation)) {
        std::array<char, 224> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws1901 stage=prepare result=fail transaction=%u equipment_selector=%llu "
            "identity_token=%llu lane=%u plug_definition=%u canonical_kind=%u model_kind=%u "
            "auxiliary=0x%llX",
            static_cast<unsigned>(message.transactionId),
            static_cast<unsigned long long>(request.equipmentSelector),
            static_cast<unsigned long long>(identityToken),
            replacement.socketIndex,
            static_cast<unsigned>(replacement.plugDefinitionIndex),
            static_cast<unsigned>(replacement.canonicalSocketKind),
            static_cast<unsigned>(replacement.modelSocketKind),
            static_cast<unsigned long long>(replacement.auxiliary));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    outcome.mutation = mutation;
    std::array<char, 288> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws1901 stage=prepare result=ok transaction=%u character=0x%llX instance=0x%llX "
        "equipment_selector=%llu identity_token=%llu target_definition=%u target_bucket=%u "
        "lane=%u plug_definition=%u plug_bucket=%u canonical_kind=%u model_kind=%u "
        "auxiliary=0x%llX",
        static_cast<unsigned>(message.transactionId),
        static_cast<unsigned long long>(mutation.characterSoid),
        static_cast<unsigned long long>(mutation.targetInstanceSoid),
        static_cast<unsigned long long>(request.equipmentSelector),
        static_cast<unsigned long long>(identityToken),
        static_cast<unsigned>(mutation.targetDefinitionIndex),
        static_cast<unsigned>(mutation.targetBucketId),
        static_cast<unsigned>(mutation.socketLane),
        static_cast<unsigned>(mutation.plugDefinitionIndex),
        static_cast<unsigned>(mutation.plugBucketId),
        static_cast<unsigned>(replacement.canonicalSocketKind),
        static_cast<unsigned>(replacement.modelSocketKind),
        static_cast<unsigned long long>(replacement.auxiliary));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Parses and prepares one complete accumulated item-state value from opcode 406. */
void mutate_item_state(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode406::Request request{};
    if (!middleware::web_service::messages::opcode406::parse_request(message, request)) {
        std::array<char, 224> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws406 stage=parse result=fail transaction=%u payload_bytes=%zu instance=0x%llX "
            "definition=%u flags=0x%X",
            static_cast<unsigned>(message.transactionId),
            message.payload.size(),
            static_cast<unsigned long long>(request.instanceSoid),
            static_cast<unsigned>(request.definitionIndex),
            request.flags);
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }

    const std::uint64_t instanceSoid = request.instanceSoid;
    const std::uint32_t flags = request.flags;
    state::PendingItemState mutation{};
    if (!state::prepare_item_state(instanceSoid, request.definitionIndex, flags, mutation)) {
        return;
    }
    outcome.mutation = mutation;
    std::array<char, 224> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws406 stage=prepare result=ok transaction=%u character=0x%llX instance=0x%llX "
        "definition=%u flags_before=0x%X flags_after=0x%X equipped=%u item_index=%zu",
        static_cast<unsigned>(message.transactionId),
        static_cast<unsigned long long>(mutation.characterSoid),
        static_cast<unsigned long long>(mutation.targetInstanceSoid),
        static_cast<unsigned>(mutation.targetDefinitionIndex),
        mutation.beforeFlags,
        mutation.afterFlags,
        mutation.targetEquipped ? 1U : 0U,
        mutation.itemIndex);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Records strict opcode-402 parsing, identity checks, and State preparation outcomes. */
void report_item_dismantle(const middleware::web_service::Message& message,
                           std::string_view result,
                           std::string_view reason,
                           std::uint64_t instanceSoid,
                           std::uint32_t definitionIndex,
                           std::uint32_t definitionHash,
                           std::uint32_t quantity) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws402 stage=prepare result=%.*s reason=%.*s transaction=%u payload_bytes=%zu "
        "instance=0x%llX definition_index=%u definition_hash=0x%08X quantity=%u",
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        static_cast<unsigned long long>(instanceSoid),
        definitionIndex,
        definitionHash,
        quantity);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         result == "ok" ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Prepares the exact fixed-width opcode-402 Character-inventory removal request. */
void dismantle_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode402::Request request{};
    if (!middleware::web_service::messages::opcode402::parse_request(message, request)) {
        report_item_dismantle(
            message, "fail", "payload_bits", request.instanceSoid, request.definitionIndex, 0, 0);
        return;
    }
    const std::uint64_t instanceSoid = request.instanceSoid;
    const std::uint16_t definitionIndex = request.definitionIndex;
    // The codec owns the value; this alias keeps the dismantle checks below readable.
    constexpr std::uint32_t kSingleQuantity =
        middleware::web_service::messages::opcode402::kSingleQuantity;

    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(definitionIndex, definition)) {
        report_item_dismantle(
            message, "fail", "definition", instanceSoid, definitionIndex, 0, kSingleQuantity);
        return;
    }
    state::vendors::Pending discard;
    if (state::vendors::prepare_postmaster_discard(instanceSoid, definitionIndex, discard)) {
        outcome.mutation = std::move(discard);
        report_item_dismantle(message, "ok", "stack_unit", instanceSoid,
                              definitionIndex, definition.definitionHash, kSingleQuantity);
        return;
    }
    state::PendingItemDismantle mutation{};
    if (!state::prepare_item_dismantle(instanceSoid, mutation)) {
        report_item_dismantle(message,
                              "fail",
                              "state",
                              instanceSoid,
                              definitionIndex,
                              definition.definitionHash,
                              kSingleQuantity);
        return;
    }
    if (mutation.dismantledItem.definitionHash != definition.definitionHash
        || mutation.dismantledItem.quantity != static_cast<std::int32_t>(kSingleQuantity)) {
        report_item_dismantle(message,
                              "fail",
                              "identity",
                              instanceSoid,
                              definitionIndex,
                              definition.definitionHash,
                              kSingleQuantity);
        return;
    }
    outcome.mutation = mutation;
    report_item_dismantle(message,
                          "ok",
                          "ready",
                          instanceSoid,
                          definitionIndex,
                          definition.definitionHash,
                          kSingleQuantity);
}

/** Records strict opcode-1820 parsing, installed mapping, and State preparation outcomes. */
void report_item_acquisition(const middleware::web_service::Message& message,
                             std::string_view result,
                             std::string_view reason,
                             std::uint32_t collectibleIndex,
                             std::uint32_t itemDefinitionIndex,
                             std::uint32_t definitionHash,
                             std::uint64_t instanceSoid) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws1820 stage=prepare result=%.*s reason=%.*s transaction=%u payload_bytes=%zu "
        "collectible_index=%u item_definition_index=%u definition_hash=0x%08X instance=0x%llX",
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        collectibleIndex,
        itemDefinitionIndex,
        definitionHash,
        static_cast<unsigned long long>(instanceSoid));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         result == "ok" ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

void report_acquisition_preparation(const middleware::web_service::Message& message,
                                    std::string_view result,
                                    std::string_view reason,
                                    std::uint32_t collectibleIndex,
                                    std::uint32_t itemDefinitionIndex,
                                    std::uint32_t definitionHash,
                                    std::uint64_t instanceSoid) noexcept {
    report_item_acquisition(message,
                            result,
                            reason,
                            collectibleIndex,
                            itemDefinitionIndex,
                            definitionHash,
                            instanceSoid);
}

/** Prepares the exact three-byte opcode-1820 Collections item request. */
void acquire_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode1820::Request request{};
    if (!middleware::web_service::messages::opcode1820::parse_request(message, request)) {
        report_item_acquisition(message,
                                "fail",
                                "payload_bits",
                                kUnavailableDefinitionIndex,
                                kUnavailableDefinitionIndex,
                                0,
                                0);
        return;
    }
    const std::uint16_t collectibleIndex = request.collectibleIndex;
    std::uint16_t itemDefinitionIndex = 0;
    if (!state::build_data::find_collectible_item_definition_index(collectibleIndex,
                                                                   itemDefinitionIndex)) {
        report_item_acquisition(message,
                                "fail",
                                "collectible_definition",
                                collectibleIndex,
                                kUnavailableDefinitionIndex,
                                0,
                                0);
        return;
    }

    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(itemDefinitionIndex, definition)) {
        report_item_acquisition(
            message, "fail", "item_definition", collectibleIndex, itemDefinitionIndex, 0, 0);
        return;
    }

    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (!state::build_data::find_configured_item_detail(itemDefinitionIndex, detail)
        || detail.definitionIndex != itemDefinitionIndex
        || detail.definitionHash != definition.definitionHash
        || detail.bucketId != definition.bucketId
        || !state::build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)) {
        report_item_acquisition(message,
                                "fail",
                                "item_detail_or_bucket",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return;
    }

    namespace bucket_domain = state::build_data::inventory::buckets;
    namespace detail_domain = state::build_data::items::details;
    if (bucket.arraySelector == bucket_domain::ArraySelector::profile) {
        if (detail.instancedDefinitionState != detail_domain::InstancedDefinitionState::stackable) {
            report_item_acquisition(message,
                                    "fail",
                                    "profile_item_instanced",
                                    collectibleIndex,
                                    itemDefinitionIndex,
                                    definition.definitionHash,
                                    0);
            return;
        }
        state::PendingProfileItemAcquisition mutation{};
        if (!state::prepare_profile_item_acquisition(
                collectibleIndex, definition.definitionHash, mutation)) {
            state::PendingItemAcquisition overflow{};
            if(state::prepare_item_acquisition(collectibleIndex,definition.definitionHash,overflow)) {
                outcome.mutation=overflow;
                report_item_acquisition(message,"ok","postmaster_ready",collectibleIndex,
                    itemDefinitionIndex,definition.definitionHash,overflow.acquiredInstanceSoid);
                return;
            }
            report_item_acquisition(message,
                                    "fail",
                                    "profile_state",
                                    collectibleIndex,
                                    itemDefinitionIndex,
                                    definition.definitionHash,
                                    0);
            return;
        }
        outcome.mutation = mutation;
        report_item_acquisition(message,
                                "ok",
                                "profile_ready",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return;
    }
    if (bucket.arraySelector != bucket_domain::ArraySelector::character) {
        report_item_acquisition(message,
                                "fail",
                                "unsupported_inventory_array",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return;
    }

    state::PendingItemAcquisition mutation{};
    if (!state::prepare_item_acquisition(collectibleIndex, definition.definitionHash, mutation)) {
        report_item_acquisition(message,
                                "fail",
                                "state",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return;
    }
    outcome.mutation = mutation;
    report_item_acquisition(message,
                            "ok",
                            "ready",
                            collectibleIndex,
                            itemDefinitionIndex,
                            definition.definitionHash,
                            mutation.acquiredInstanceSoid);
}

/**
 * Writes one purchase line.
 *
 * The opcode is carried rather than hard-coded: 901 and 904 share this line, and a quest acquire
 * reporting itself as `ws901` sends anyone reading the log to the wrong decoder.
 *
 * @param opcode Request opcode the line belongs to, 901 or 904.
 * @param result `ok` or `fail`.
 * @param reason Step that decided it.
 * @param vendorIndex Vendor row the request named.
 * @param saleIndex Sale row the request named.
 * @param itemDefinitionIndex Item resolved, when the row resolved.
 */
void report_purchase(std::uint16_t opcode,
                     const char* result,
                     const char* reason,
                     std::int32_t vendorIndex,
                     std::int32_t saleIndex,
                     std::uint16_t itemDefinitionIndex) noexcept {
    core::log::writef(core::log::Channel::server,
                      std::strcmp(result, "ok") == 0 ? core::log::Level::info
                                                     : core::log::Level::warn,
                      "ev=ws%u stage=purchase result=%s reason=%s vendor=%d sale=%d item=%u",
                      static_cast<unsigned>(opcode),
                      result,
                      reason,
                      static_cast<int>(vendorIndex),
                      static_cast<int>(saleIndex),
                      static_cast<unsigned>(itemDefinitionIndex));
}

/**
 * Resolves the vendor a request names to its index row and held definition.
 *
 * Every vendor behaviour starts here, and five of them spelled it out by hand. A negative index is
 * the client's own absent marker and never a row.
 *
 * @param vendorIndex Vendor row the request named.
 * @param entry Receives the index row.
 * @param definition Receives the held definition.
 * @return True when the row exists and its definition is published.
 */
[[nodiscard]] bool find_vendor(std::int32_t vendorIndex,
                               state::build_data::vendors::IndexEntry& entry,
                               state::build_data::vendors::Definition& definition) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    entry = {};
    definition = {};
    return vendorIndex >= 0 && vendorIndex <= (std::numeric_limits<std::uint16_t>::max)()
           && vendor_domain::find_index(static_cast<std::uint16_t>(vendorIndex), entry)
           && vendor_domain::find(entry.definitionHash, definition);
}

/** What a substitution rule said about one sale row's item. */
enum class Substitution : std::uint8_t {
    /** No rule names this item; the row grants what it names. */
    none,
    /** A rule names it and its replacement resolved; the row grants the replacement. */
    replaced,
    /** A rule names it but its replacement is not in this build; the row must grant nothing. */
    broken,
};

/**
 * Answers what a placeholder sale row is really selling.
 *
 * Several rows name a DestinyItemType 20 Dummy - a UI placeholder for something the row does not
 * name, as Amanda Holliday's Legacy Content rows stand for a campaign's first quest step. Granting
 * the placeholder puts an item in the Quests tab the client will not draw, and the row never
 * settles. `vendor_item_substitute.txt` maps sold hash to granted hash, keyed by item so one rule
 * covers every seller. A rule whose replacement is absent from this build answers `broken` rather
 * than `none`: the rule proves the row's item is a placeholder, and granting it would be the exact
 * wrong grant this file exists to prevent.
 *
 * @param itemDefinitionIndex Item the row resolved to.
 * @param substituteIndex Receives what should be granted in its place.
 * @return What the rule file said about this item.
 */
[[nodiscard]] Substitution substitute_for_item(std::uint16_t itemDefinitionIndex,
                                               std::uint16_t& substituteIndex) noexcept {
    substituteIndex = kUnavailableDefinitionIndex;
    state::build_data::items::Definition sold{};
    if (!state::build_data::find_item_definition_index(itemDefinitionIndex, sold)) {
        return Substitution::none;
    }
    if (!core::path::read_artifact_text(L"vendor_item_substitute.txt", g_ruleText)) {
        return Substitution::none;
    }
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (rules.seek_field()) {
        const std::uint32_t soldHash = rules.read_hex();
        const std::uint32_t grantHash = rules.read_hex();
        if (soldHash != sold.definitionHash) {
            continue;
        }
        state::build_data::items::Definition replacement{};
        const bool resolved =
            state::build_data::find_item_definition_hash(grantHash, replacement);
        if (resolved) {
            substituteIndex = replacement.definitionIndex;
        }
        if (resolved) {
            core::log::writef(core::log::Channel::server,
                              core::log::Level::info,
                              "ev=vendor stage=substitute sold=0x%08X granted=0x%08X item=%u",
                              sold.definitionHash,
                              replacement.definitionHash,
                              static_cast<unsigned>(replacement.definitionIndex));
            return Substitution::replaced;
        }
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=vendor stage=substitute result=fail reason=missing sold=0x%08X "
                          "named=0x%08X",
                          sold.definitionHash,
                          grantHash);
        return Substitution::broken;
    }
    return Substitution::none;
}

/**
 * Rolls one random unheld repeatable bounty, for a row that offers "Additional Bounties".
 *
 * The row sells a Dummy placeholder; what it owes is a REPEATABLE bounty, a distinct kind a
 * character may hold five of. The pool is authored by hash in `vendor_bounty_roll.txt`, because a
 * repeatable is not a sale row - no vendor in the manifest lists one - so nothing on the vendor can
 * be discovered or picked from. Rules are keyed by vendor definition hash and trigger category,
 * since one vendor can own several such rows (Eva Levante has one per event), and lines sharing a
 * key accumulate. A hash this build does not carry is skipped, so a pool authored from a newer
 * manifest degrades to what exists rather than failing whole.
 *
 * @param vendorIndex Vendor the purchase names.
 * @param categoryIndex Category of the purchased row, from sale row +100.
 * @param rolledItemIndex Receives the bounty to grant.
 * @return True when this row is a bounty roll and its own item must NOT be granted.
 */
[[nodiscard]] bool roll_vendor_bounty(std::int32_t vendorIndex,
                                      std::int32_t categoryIndex,
                                      std::uint16_t& rolledItemIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    rolledItemIndex = kUnavailableDefinitionIndex;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (categoryIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    if (!core::path::read_artifact_text(L"vendor_bounty_roll.txt", g_ruleText)) {
        return false;
    }
    // Every hash authored for this exact key. Lines carrying the same key accumulate, so the pool
    // is gathered from the whole file rather than from the first line that matches.
    std::array<std::uint32_t, kRepeatablePoolCapacity> pool{};
    std::size_t poolCount = 0;
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (rules.seek_field()) {
        const std::uint32_t ruleHash = rules.read_hex();
        const std::int32_t ruleCategory = rules.read_decimal();
        const bool wanted = ruleHash == entry.definitionHash && ruleCategory == categoryIndex;
        // The rest of the line is item hashes. A newline is not a rule field, so this stops at the
        // end of the line without needing to look for one.
        while (rules.at_field()) {
            const std::uint32_t itemHash = rules.read_hex();
            if (wanted && poolCount < pool.size()) {
                pool[poolCount++] = itemHash;
            }
        }
    }
    if (poolCount == 0) {
        return false;
    }
    // Reservoir pick over what this build actually carries and the character does not already hold,
    // so the pool is walked once and no count is needed up front.
    std::uint32_t resolved = 0;
    std::uint32_t held = 0;
    std::uint32_t candidates = 0;
    std::uint64_t seed =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    // One account view for the whole pool. Reading it copies the whole account, and the pool is
    // walked candidate by candidate, so taking it per candidate would copy it dozens of times to
    // answer dozens of questions about the same unchanging view.
    const state::AccountState account = state::account_snapshot();
    for (std::size_t at = 0; at < poolCount; ++at) {
        state::build_data::items::Definition item{};
        if (!state::build_data::items::find_hash(pool[at], item)) {
            continue;
        }
        ++resolved;
        if (state::account::holds_pursuit(account, item.definitionIndex)) {
            ++held;
            continue;
        }
        ++candidates;
        seed = (seed * 6364136223846793005ULL) + 1442695040888963407ULL;
        if ((seed >> 33) % candidates == 0) {
            rolledItemIndex = item.definitionIndex;
        }
    }
    // Retail lets a character keep five of a vendor's repeatables at once. Refusing here rather
    // than at the grant keeps the roll from consuming a pick it would only have to throw away.
    if (held >= kRepeatableHoldLimit) {
        rolledItemIndex = kUnavailableDefinitionIndex;
    }
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=bounty_roll stage=pick vendor=%d hash=0x%08X category=%d authored=%u "
                      "resolved=%u held=%u pool=%u item=%d",
                      vendorIndex,
                      entry.definitionHash,
                      categoryIndex,
                      static_cast<unsigned>(poolCount),
                      resolved,
                      held,
                      candidates,
                      rolledItemIndex == kUnavailableDefinitionIndex
                          ? -1
                          : static_cast<int>(rolledItemIndex));
    return true;
}

/**
 * Runs a vendor's recycle row: charges the stack it names and credits what it pays out.
 *
 * The Drifter's four Synth Recycling rows take five synths each; Master Rahool's Recycle Shaders
 * category has one row per shader, 277 of them. The cost is authored in `vendor_exchange.txt`
 * rather than read off the row, because the sale row's cost-bearing fields are still role-open on
 * this build; the manifest's row order is this build's (304 rows checked against Lord Shaxx). A
 * rule is `<vendor> <row> <costItem> <costQuantity>` then `<payoutItem> <payoutQuantity>` pairs.
 *
 * @param vendorIndex Vendor the purchase names.
 * @param rowIndex Sale row the purchase names.
 * @param mutation Receives the prepared profile-stack change.
 * @return True when this row was an exchange and its own item must NOT be granted.
 */
[[nodiscard]] bool
exchange_vendor_row(std::int32_t vendorIndex,
                    std::int32_t rowIndex,
                    state::PendingProfileItemAcquisition& mutation) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (rowIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    if (!core::path::read_artifact_text(L"vendor_exchange.txt", g_ruleText)) {
        return false;
    }
    std::uint32_t costHash = 0;
    std::int32_t costQuantity = 0;
    std::array<state::ProfileExchangePayout, kExchangePayoutCapacity> payouts{};
    std::size_t payoutCount = 0;
    bool matched = false;
    bool overflowed = false;
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (!matched && rules.seek_field()) {
        const std::uint32_t ruleVendor = rules.read_hex();
        const std::int32_t ruleRow = rules.read_decimal();
        const std::uint32_t ruleCost = rules.read_hex();
        const std::int32_t ruleCostQuantity = rules.read_decimal();
        // The rest of the line is payout pairs, and every one of them is consumed even past what
        // can be held. Stopping mid-line would leave the fields that did not fit to be read as the
        // start of the next rule, turning one over-long rule into a second, invented one.
        std::array<state::ProfileExchangePayout, kExchangePayoutCapacity> rulePayouts{};
        std::size_t rulePayoutCount = 0;
        bool ruleOverflowed = false;
        while (rules.at_field()) {
            const std::uint32_t payoutHash = rules.read_hex();
            const std::int32_t payoutQuantity = rules.read_decimal();
            if (rulePayoutCount < rulePayouts.size()) {
                rulePayouts[rulePayoutCount++] = {payoutHash, payoutQuantity};
            } else {
                ruleOverflowed = true;
            }
        }
        matched = ruleVendor == entry.definitionHash && ruleRow == rowIndex;
        if (matched) {
            overflowed = ruleOverflowed;
            costHash = ruleCost;
            costQuantity = ruleCostQuantity;
            payouts = rulePayouts;
            payoutCount = rulePayoutCount;
        }
    }
    if (!matched) {
        return false;
    }
    // A matched rule owns the row whatever else it got wrong, because the rule proves the row's
    // own item is a placeholder and falling through would grant it. A rule naming more payouts
    // than the change ring can announce, or none at all, is refused whole rather than paid in
    // part - and the refusal is logged, because a rule that silently does nothing reads exactly
    // like a rule that was never written.
    if (overflowed || payoutCount == 0) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=vendor_exchange stage=apply result=fail reason=%s vendor=%d "
                          "hash=0x%08X row=%d payouts=%zu limit=%zu",
                          overflowed ? "payout_overflow" : "payout_missing",
                          vendorIndex,
                          entry.definitionHash,
                          rowIndex,
                          payoutCount,
                          kExchangePayoutCapacity);
        return true;
    }
    const bool applied = state::prepare_vendor_exchange(
        costHash, costQuantity,
        std::span<const state::ProfileExchangePayout>{payouts.data(), payoutCount}, mutation);
    core::log::writef(core::log::Channel::server,
                      applied ? core::log::Level::info : core::log::Level::warn,
                      "ev=vendor_exchange stage=apply result=%s vendor=%d hash=0x%08X row=%d "
                      "cost=0x%08X quantity=%d payouts=%zu",
                      applied ? "ok" : "fail",
                      vendorIndex,
                      entry.definitionHash,
                      rowIndex,
                      costHash,
                      costQuantity,
                      payoutCount);
    // Even a refused exchange owns the row. Falling through would grant the Dummy placeholder,
    // which is the failure this whole path exists to avoid.
    return true;
}

namespace {

/**
 * Cost rows one authored vendor price may carry.
 *
 * The charge template refuses a span wider than this, so it is the reader's limit too. The widest
 * price Eva actually authors is three rows (sale row 10, BrayTech Werewolf Random Roll); the
 * headroom costs nothing and keeps the reader from being the thing that has to change when a
 * wider row is authored.
 */
constexpr std::size_t kPriceCapacity =
    state::build_data::material_requirements::kRequirementCapacity;

/** What the last reported price-file load said, so the same answer is never said twice. */
enum class VendorPriceLoadReport : std::uint8_t {
    /** Nothing has been reported yet this process. */
    unreported,
    /** The last line said the file was not there. */
    absent,
    /** The last line said the file was there, and how many rules it carried. */
    present,
};
VendorPriceLoadReport g_vendorPriceLoadReport = VendorPriceLoadReport::unreported;
/** Rules the last `present` line reported, so a re-read of the same file stays silent. */
std::size_t g_vendorPriceLoadRules = 0;

} // namespace

/** One authored vendor price, resolved to the exact rows the charge template consumes. */
struct VendorPrice final {
    /** Charge rows, in the shape `apply_material_requirements` reads. */
    std::array<state::build_data::material_requirements::Requirement, kPriceCapacity> rows{};
    /** The authored cost hashes, kept beside the rows so a refusal can name the currency. */
    std::array<std::uint32_t, kPriceCapacity> hashes{};
    std::size_t count{};

    /** @return The charge this price applies; empty when the row authors no price. */
    [[nodiscard]] std::span<const state::build_data::material_requirements::Requirement>
    charge() const noexcept {
        return {rows.data(), count};
    }
};

/** What the price file said about one sale row. */
enum class PriceLookup : std::uint8_t {
    /** No rule names this row; it keeps the uncharged behaviour this build shipped with. */
    none,
    /** A rule names it and every cost row resolved; charge it. */
    priced,
    /** A rule names it and could not be resolved whole; refuse rather than sell it for nothing. */
    broken,
};

/**
 * Says once whether authored vendor prices are installed at all.
 *
 * No server boot step reads rule files - every reader opens its own file per request - so the one
 * line that answers "are prices on, and how many rules" is written by the first lookup after
 * activation instead of at boot proper. A missing file is not a failure: no price file is exactly
 * the behaviour this build had before one existed.
 *
 * The line is latched on its own ANSWER rather than on "something was said", because the file
 * is hot-installable: nothing rereads it at boot, so the ordinary install order is a purchase that
 * finds no file, then the operator dropping one in. Latching on "reported" would leave that
 * session saying `result=absent` forever while every later purchase was in fact being charged.
 *
 * @param present Whether `vendor_price.txt` was readable.
 * @param ruleCount Rules the file carried, when it was.
 */
void report_vendor_price_load(bool present, std::size_t ruleCount) noexcept {
    const VendorPriceLoadReport answer =
        present ? VendorPriceLoadReport::present : VendorPriceLoadReport::absent;
    if (g_vendorPriceLoadReport == answer
        && (!present || g_vendorPriceLoadRules == ruleCount)) {
        return;
    }
    g_vendorPriceLoadReport = answer;
    g_vendorPriceLoadRules = present ? ruleCount : 0;
    if (!present) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=vendor_price stage=load result=absent");
        return;
    }
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=vendor_price stage=load result=ok rows=%zu",
                      ruleCount);
}

/**
 * Resolves what one vendor sale row charges.
 *
 * The cost is authored in `vendor_price.txt` rather than read off the row, for the same reason
 * `vendor_exchange.txt` exists: the sale row's cost-bearing fields are still role-open on this
 * build. A rule is `<vendor> <row> <costItem> <costQuantity>` then further
 * `<costItem> <costQuantity>` pairs, all costs, up to the charge template's own row limit.
 *
 * This MUST be its own file. To `core::rule_text::Cursor` a price rule and an exchange rule are
 * the same shape, but `exchange_vendor_row` reads the trailing pairs as PAYOUTS - merged, a row
 * that means "charge 5000 Glimmer" would credit 5000 Glimmer instead.
 *
 * A rule that names a row but cannot be resolved answers `broken` rather than `none`: the rule
 * proves the row is meant to cost something, and falling through would hand it over free, which
 * is the exact failure this file exists to prevent.
 *
 * @param vendorIndex Vendor the request names.
 * @param rowIndex Sale row the request names.
 * @param price Receives the resolved charge; cleared unless the answer is `priced`.
 * @return What the rule file said about this row.
 */
[[nodiscard]] PriceLookup price_for_vendor_row(std::int32_t vendorIndex,
                                               std::int32_t rowIndex,
                                               VendorPrice& price) noexcept {
    namespace requirement_domain = state::build_data::material_requirements;
    namespace vendor_domain = state::build_data::vendors;
    price = {};
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (rowIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return PriceLookup::none;
    }
    if (!core::path::read_artifact_text(L"vendor_price.txt", g_ruleText)) {
        report_vendor_price_load(false, 0);
        return PriceLookup::none;
    }
    // Values are copied out of the shared rule buffer as they are read: `g_ruleText` is one
    // buffer for every rule file, and whatever runs next overwrites it.
    std::array<std::uint32_t, kPriceCapacity> hashes{};
    std::array<std::int32_t, kPriceCapacity> quantities{};
    std::size_t pairCount = 0;
    std::size_t ruleCount = 0;
    bool matched = false;
    bool overflowed = false;
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (rules.seek_field()) {
        const std::uint32_t ruleVendor = rules.read_hex();
        const std::int32_t ruleRow = rules.read_decimal();
        std::array<std::uint32_t, kPriceCapacity> ruleHashes{};
        std::array<std::int32_t, kPriceCapacity> ruleQuantities{};
        std::size_t rulePairs = 0;
        bool ruleOverflowed = false;
        // The rest of the line is cost pairs, and every one of them is consumed even past what
        // can be held. Stopping mid-line would leave the fields that did not fit to be read as
        // the start of the next rule, turning one over-long rule into a second, invented one.
        while (rules.at_field()) {
            const std::uint32_t costHash = rules.read_hex();
            const std::int32_t costQuantity = rules.read_decimal();
            if (rulePairs < kPriceCapacity) {
                ruleHashes[rulePairs] = costHash;
                ruleQuantities[rulePairs] = costQuantity;
                ++rulePairs;
            } else {
                ruleOverflowed = true;
            }
        }
        ++ruleCount;
        if (matched || ruleVendor != entry.definitionHash || ruleRow != rowIndex) {
            continue;
        }
        matched = true;
        overflowed = ruleOverflowed;
        hashes = ruleHashes;
        quantities = ruleQuantities;
        pairCount = rulePairs;
    }
    report_vendor_price_load(true, ruleCount);
    if (!matched) {
        return PriceLookup::none;
    }
    if (overflowed || pairCount == 0) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=vendor_price stage=apply result=fail reason=%s vendor=%d "
                          "hash=0x%08X row=%d costs=%zu limit=%zu",
                          overflowed ? "cost_overflow" : "cost_missing",
                          vendorIndex,
                          entry.definitionHash,
                          rowIndex,
                          pairCount,
                          kPriceCapacity);
        return PriceLookup::broken;
    }
    for (std::size_t at = 0; at < pairCount; ++at) {
        // The charge template keys on the installed item index, not the authored hash, so a cost
        // this build does not carry cannot be charged and must not be quietly dropped.
        state::build_data::items::Definition costItem{};
        if (quantities[at] <= 0
            || !state::build_data::find_item_definition_hash(hashes[at], costItem)) {
            core::log::writef(core::log::Channel::server,
                              core::log::Level::warn,
                              "ev=vendor_price stage=apply result=fail reason=%s vendor=%d "
                              "hash=0x%08X row=%d cost=0x%08X quantity=%d",
                              quantities[at] <= 0 ? "cost_quantity" : "cost_unresolved",
                              vendorIndex,
                              entry.definitionHash,
                              rowIndex,
                              hashes[at],
                              quantities[at]);
            price = {};
            return PriceLookup::broken;
        }
        price.hashes[at] = costItem.definitionHash;
        price.rows[at].quantity = static_cast<std::uint32_t>(quantities[at]);
        price.rows[at].itemDefinitionIndex = costItem.definitionIndex;
        price.rows[at].condition = requirement_domain::kUnconditionalRequirement;
        price.rows[at].deleteOnAction = true;
        price.rows[at].omitFromRequirements = false;
    }
    price.count = pairCount;
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=vendor_price stage=apply result=ok vendor=%d hash=0x%08X row=%d "
                      "costs=%zu first=0x%08X quantity=%d",
                      vendorIndex,
                      entry.definitionHash,
                      rowIndex,
                      pairCount,
                      price.hashes[0],
                      static_cast<int>(price.rows[0].quantity));
    return PriceLookup::priced;
}

/**
 * Names the authored cost row a refused priced purchase could not pay.
 *
 * `prepare_*` answers only true or false, so the purchase line alone cannot tell "could not
 * afford" from "bucket full". This re-reads the same balances the charge gates on - the sum of
 * every non-instanced stack of that definition - purely to name the short row in the log. It
 * decides nothing: a purchase is refused by State, never by this.
 *
 * @param opcode Request opcode the line belongs to, 901 or 904.
 * @param vendorIndex Vendor the request names.
 * @param rowIndex Sale row the request names.
 * @param itemDefinitionIndex Item the grant was refused for.
 * @param price Price that was handed to the refused grant.
 * @return True when a cost row is short, which is what makes `reason=price` the honest reason.
 */
[[nodiscard]] bool report_price_refusal(std::uint16_t opcode,
                                        std::int32_t vendorIndex,
                                        std::int32_t rowIndex,
                                        std::uint16_t itemDefinitionIndex,
                                        const VendorPrice& price) noexcept {
    if (price.count == 0) {
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    for (std::size_t at = 0; at < price.count; ++at) {
        std::int64_t available = 0;
        for (std::size_t index = 0; index < account.profileItemCount; ++index) {
            const state::account::inventory::ProfileItem& item = account.profileItems[index];
            if (item.definitionHash == price.hashes[at] && item.instanceSoid == 0
                && item.quantity > 0) {
                available += item.quantity;
            }
        }
        if (available >= static_cast<std::int64_t>(price.rows[at].quantity)) {
            continue;
        }
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=ws%u stage=purchase result=fail reason=price vendor=%d row=%d "
                          "item=%u cost=0x%08X quantity=%d have=%d",
                          static_cast<unsigned>(opcode),
                          vendorIndex,
                          rowIndex,
                          static_cast<unsigned>(itemDefinitionIndex),
                          price.hashes[at],
                          static_cast<int>(price.rows[at].quantity),
                          static_cast<int>(available));
        return true;
    }
    return false;
}

/** How one grant ended, so a caller can tell a settled row from a row still owed its item. */
enum class GrantResult : std::uint8_t {
    /** The item is prepared for the inventory; the row's offer is answered. */
    granted,
    /** The character already holds this pursuit, so the offer was answered some time ago. */
    alreadyHeld,
    /** Nothing was granted and nothing was held; the offer still stands. */
    refused,
};

/**
 * Grants one item, given the collectible that owns it and its definition index.
 *
 * Split out of `acquire_item` so a vendor purchase reaches the same grant instead of growing a
 * second acquisition path. Character gear is a direct reward with its vendor price; its
 * Collections identity is retained for diagnostics without charging a reclaim price.
 *
 * An authored cost rides the same prepared transaction as the grant, so the item and the charge
 * commit together or not at all. An empty cost is the uncharged grant this build shipped with.
 *
 * @param message Request being answered, for the log line.
 * @param collectibleIndex Collectible that owns the item.
 * @param itemDefinitionIndex Item to grant.
 * @param outcome Receives the prepared mutation on success.
 * @param cost Authored cost charged inside the same transaction; empty charges nothing.
 * @return How the grant ended, which is what decides whether the row's offer was answered.
 */
GrantResult grant_item_definition(
    const middleware::web_service::Message& message,
    std::uint16_t collectibleIndex,
    std::uint16_t itemDefinitionIndex,
    Outcome& outcome,
    std::span<const state::build_data::material_requirements::Requirement> cost = {}) noexcept {
    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(itemDefinitionIndex, definition)) {
        report_acquisition_preparation(
            message, "fail", "item_definition", collectibleIndex, itemDefinitionIndex, 0, 0);
        return GrantResult::refused;
    }
    // The same rule the client's native vendor-row gate applies locally, so a row that is still
    // offered can never be one this grant would refuse.
    if (state::account::holds_pursuit(itemDefinitionIndex)) {
        report_acquisition_preparation(message,
                                       "fail",
                                       "already_held",
                                       collectibleIndex,
                                       itemDefinitionIndex,
                                       definition.definitionHash,
                                       0);
        return GrantResult::alreadyHeld;
    }

    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (!state::build_data::find_configured_item_detail(itemDefinitionIndex, detail)
        || detail.definitionIndex != itemDefinitionIndex
        || detail.definitionHash != definition.definitionHash
        || detail.bucketId != definition.bucketId
        || !state::build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)) {
        report_acquisition_preparation(message,
                                       "fail",
                                       "item_detail_or_bucket",
                                       collectibleIndex,
                                       itemDefinitionIndex,
                                       definition.definitionHash,
                                       0);
        return GrantResult::refused;
    }

    namespace bucket_domain = state::build_data::inventory::buckets;
    namespace detail_domain = state::build_data::items::details;
    if (bucket.arraySelector == bucket_domain::ArraySelector::profile) {
        if (detail.instancedDefinitionState != detail_domain::InstancedDefinitionState::stackable) {
            report_acquisition_preparation(message,
                                           "fail",
                                           "profile_item_instanced",
                                           collectibleIndex,
                                           itemDefinitionIndex,
                                           definition.definitionHash,
                                           0);
            return GrantResult::refused;
        }
        state::PendingProfileItemAcquisition mutation{};
        if (!state::prepare_profile_item_acquisition(
                collectibleIndex, definition.definitionHash, mutation, cost)) {
            report_acquisition_preparation(message,
                                           "fail",
                                           "profile_state",
                                           collectibleIndex,
                                           itemDefinitionIndex,
                                           definition.definitionHash,
                                           0);
            return GrantResult::refused;
        }
        outcome.mutation = mutation;
        report_acquisition_preparation(message,
                                       "ok",
                                       "profile_ready",
                                       collectibleIndex,
                                       itemDefinitionIndex,
                                       definition.definitionHash,
                                       0);
        return GrantResult::granted;
    }
    if (bucket.arraySelector != bucket_domain::ArraySelector::character) {
        report_acquisition_preparation(message,
                                       "fail",
                                       "unsupported_inventory_array",
                                       collectibleIndex,
                                       itemDefinitionIndex,
                                       definition.definitionHash,
                                       0);
        return GrantResult::refused;
    }

    state::PendingItemAcquisition mutation{};
    if (!state::prepare_item_acquisition_for_item(
            itemDefinitionIndex, mutation, {}, cost)) {
        report_acquisition_preparation(message,
                                       "fail",
                                       "state",
                                       collectibleIndex,
                                       itemDefinitionIndex,
                                       definition.definitionHash,
                                       0);
        return GrantResult::refused;
    }
    outcome.mutation = mutation;
    report_acquisition_preparation(message,
                                   "ok",
                                   "ready",
                                   collectibleIndex,
                                   itemDefinitionIndex,
                                   definition.definitionHash,
                                   mutation.acquiredInstanceSoid);
    return GrantResult::granted;
}

[[nodiscard]] bool festival_of_the_lost_active() noexcept;

/** Installed Festival offers distinguish the curated weapon from its random-roll voucher. */
constexpr std::uint32_t kWerewolfRandomOffer = 0xD9469DB5U;
constexpr std::uint32_t kWerewolfWeapon = 0x1F855E14U;
constexpr std::uint32_t kFestivalLegendaryShards = 0x3CF2E8E2U;

/** Eva's repeatable voucher produces gear, charging its three authored materials once. */
[[nodiscard]] bool prepare_festival_werewolf(std::int32_t vendorIndex,
                                             std::uint32_t offer,
                                             Outcome& outcome) noexcept {
    namespace data = state::build_data;
    data::vendors::IndexEntry vendor{};
    data::vendors::Definition vendorDefinition{};
    if (!find_vendor(vendorIndex, vendor, vendorDefinition)
        || vendor.definitionHash != festival_bags::kEva || !festival_of_the_lost_active()) return false;
    const bool random = offer == kWerewolfRandomOffer;
    // Installed Eva offer: curated 1,000 Candy, or random 250 Candy + 5,000 Glimmer + 5 Shards.
    const std::array<state::ProfileExchangePayout, 3> materials{{
        {festival_bags::kCandy, random ? 250 : 1000},
        {festival_bags::kGlimmer, 5000}, {kFestivalLegendaryShards, 5}}};
    const std::size_t count = random ? materials.size() : 1;
    std::array<data::material_requirements::Requirement, 3> cost{};
    for (std::size_t i = 0; i < count; ++i) {
        data::items::Definition material{};
        if (!data::find_item_definition_hash(materials[i].definitionHash, material)) return false;
        cost[i] = {static_cast<std::uint32_t>(materials[i].quantity), material.definitionIndex,
                   data::material_requirements::kUnconditionalRequirement, true, false};
    }
    data::items::Definition weapon{};
    state::PendingItemAcquisition mutation{};
    if (!data::find_item_definition_hash(kWerewolfWeapon, weapon)
        || !state::prepare_item_acquisition_for_item(weapon.definitionIndex, mutation,
              {.allowRandomRoll = random}, std::span(cost).first(count))) return false;
    outcome.mutation = mutation;
    core::log::writef(core::log::Channel::server, core::log::Level::info,
        "ev=festival_werewolf stage=prepared random=%u instance=0x%llX", random ? 1U : 0U,
        static_cast<unsigned long long>(mutation.acquiredInstanceSoid));
    return true;
}

/** Auto-use packages settle as one gear acquisition plus profile rewards, never as a bag. */
[[nodiscard]] bool prepare_festival_bag(std::int32_t vendorIndex, std::uint32_t bag,
    Outcome& outcome) noexcept {
    namespace data = state::build_data;
    data::vendors::IndexEntry vendor{};
    data::vendors::Definition vendorDefinition{};
    if (!find_vendor(vendorIndex, vendor, vendorDefinition)
        || vendor.definitionHash != festival_bags::kEva || !festival_of_the_lost_active()) return false;
    const auto account = state::account_snapshot();
    const state::CharacterState* character = nullptr;
    for (std::size_t i = 0; i < account.characterCount; ++i) {
        if (account.characters[i].selected) character = &account.characters[i];
    }
    if (!character) return false;
    std::array<std::uint32_t, festival_bags::kRareGear.size() + festival_bags::kLegendaryGear.size()> eligible{};
    std::size_t count{};
    for (const auto& gear : festival_bags::gear_pool(bag)) {
        if (gear.characterClass != 3 && gear.characterClass != static_cast<std::uint8_t>(character->characterClass)) continue;
        data::items::Definition item{};
        data::items::details::Definition detail{};
        data::inventory::buckets::Descriptor bucket{};
        if (data::find_item_definition_hash(gear.hash, item)
            && data::find_configured_item_detail(item.definitionIndex, detail)
            && detail.definitionHash == gear.hash && detail.bucketId == item.bucketId
            && detail.equipmentSlot.has_value()
            && detail.instancedDefinitionState == data::items::details::InstancedDefinitionState::instanced
            && data::find_inventory_bucket_descriptor(detail.bucketId, bucket)
            && bucket.arraySelector == data::inventory::buckets::ArraySelector::character) {
            eligible[count++] = gear.hash;
        }
    }
    if (!count) return false;
    static std::atomic<std::uint64_t> sequence{};
    std::uint64_t seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
        ^ sequence.fetch_add(1, std::memory_order_relaxed) ^ character->soid;
    const auto plan = festival_bags::roll(bag, seed);
    std::array<data::material_requirements::Requirement, 2> cost{};
    for (std::size_t i = 0; i < plan.costCount; ++i) {
        data::items::Definition item{};
        if (!data::find_item_definition_hash(plan.costs[i].definitionHash, item)) return false;
        cost[i].itemDefinitionIndex = item.definitionIndex;
        cost[i].quantity = static_cast<std::uint32_t>(plan.costs[i].quantity);
        cost[i].deleteOnAction = true;
    }
    const auto gear = eligible[festival_bags::next(seed) % count];
    state::PendingItemAcquisition mutation{};
    // No collectible: a package reward must not also charge the Collections reclaim price.
    if (!state::prepare_item_acquisition(data::collectibles::kNoCollectibleIndex, gear, mutation,
            std::span(cost).first(plan.costCount))
        || !state::stage_item_profile_rewards(mutation, std::span(plan.materials).first(plan.materialCount))) return false;
    outcome.mutation = mutation;
    core::log::writef(core::log::Channel::server, core::log::Level::info,
        "ev=festival_bag stage=prepared bag=0x%08X gear=0x%08X pool=%zu reward0=0x%08X quantity0=%d reward1=0x%08X quantity1=%d",
        bag, gear, count, plan.materials[0].definitionHash, plan.materials[0].quantity,
        plan.materials[1].definitionHash, plan.materials[1].quantity);
    return true;
}

/**
 * Finds the collectible that owns one item definition.
 *
 * A sale row names an item, never a collectible, while the acquisition state is keyed by
 * collectible. Bounties, tokens and quest steps have none at all; those are granted by hash under
 * `kNoCollectibleIndex`, which is why the caller's sentinel is left in place when nothing matches.
 *
 * @param itemDefinitionIndex Item to look up.
 * @param collectibleIndex Receives the owning collectible row; untouched when none does.
 * @return True when a collectible names this item.
 */
[[nodiscard]] bool find_collectible_for_item(std::uint16_t itemDefinitionIndex,
                                             std::uint16_t& collectibleIndex) noexcept {
    return state::build_data::collectibles::find_granting(itemDefinitionIndex, collectibleIndex);
}

/**
 * Resolves one vendor row to the item it sells.
 *
 * Shared by the purchase (901) and the quest acquire (904), which name a row the same way, so the
 * two cannot drift apart.
 *
 * @param vendorIndex Vendor table row.
 * @param rowIndex Sale row within that vendor.
 * @param itemDefinitionIndex Receives the item the row sells.
 * @param reason Receives the step that failed, when one does.
 * @return True when the row resolved.
 */
[[nodiscard]] bool resolve_vendor_row(std::int32_t vendorIndex,
                                      std::int32_t rowIndex,
                                      std::uint16_t& itemDefinitionIndex,
                                      std::int32_t& categoryIndex,
                                      const char*& reason) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    if (vendorIndex < 0 || rowIndex < 0) {
        reason = "negative_index";
        return false;
    }
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (!find_vendor(vendorIndex, entry, definition)) {
        reason = "vendor";
        return false;
    }
    vendor_domain::SaleRow row{};
    if (!vendor_domain::sale_row(definition, static_cast<std::size_t>(rowIndex), row)) {
        reason = "sale_row";
        return false;
    }
    itemDefinitionIndex = row.itemIndex;
    categoryIndex = row.categoryIndex;
    return true;
}

/** Pursuit rows written out when a vendor is asked what it actually sells. */
constexpr std::size_t kPursuitListCap = 64;

/**
 * Lists the sale rows of one vendor whose item is a pursuit, when a rowless tile fails to resolve.
 *
 * It says what this vendor does offer that would land in the Quests tab, which is the difference
 * between "this click is broken" and "this click was never a quest". Items rather than rows, because
 * one placeholder repeats across dozens of rows. The classification is the shared pursuit rule.
 *
 * @param vendorIndex Vendor to list.
 */
void report_pursuit_rows(std::int32_t vendorIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    namespace detail_domain = state::build_data::items::details;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (!find_vendor(vendorIndex, entry, definition)) {
        return;
    }
    const std::size_t count = definition.saleCount;
    // One item repeats across dozens of rows - Amanda declares 38 consecutive rows of a single
    // placeholder - so listing rows rather than items buries everything interesting under filler.
    static std::array<std::uint16_t, kPursuitListCap> seen{};
    std::size_t listed = 0;
    std::size_t pursuits = 0;
    for (std::size_t row = 0; row < count; ++row) {
        vendor_domain::SaleRow sale{};
        if (!vendor_domain::sale_row(definition, row, sale)) {
            break;
        }
        const std::uint16_t itemIndex = sale.itemIndex;
        detail_domain::Definition detail{};
        if (!state::build_data::find_configured_item_detail(itemIndex, detail)
            || detail.equipmentSlot.has_value() || detail.maxStackSize > 1) {
            continue;
        }
        ++pursuits;
        bool duplicate = false;
        for (std::size_t index = 0; index < listed; ++index) {
            duplicate = duplicate || seen[index] == itemIndex;
        }
        if (duplicate || listed >= kPursuitListCap) {
            continue;
        }
        seen[listed] = itemIndex;
        ++listed;
        // One line per distinct row, so this is the detail behind the summary rather than
        // something worth putting in front of everything else that reports at info.
        core::log::writef(core::log::Channel::server,
                          core::log::Level::debug,
                          "ev=vendor stage=pursuit vendor=%d sale=%zu item=%u hash=0x%08X "
                          "bucket=%u",
                          static_cast<int>(vendorIndex),
                          row,
                          static_cast<unsigned>(itemIndex),
                          detail.definitionHash,
                          static_cast<unsigned>(detail.bucketId));
    }
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=vendor stage=pursuits vendor=%d sale_rows=%zu pursuits=%zu "
                      "distinct_listed=%zu",
                      static_cast<int>(vendorIndex),
                      count,
                      pursuits,
                      listed);
}

/** An installed row names its item by definition hash at this offset. */
constexpr std::size_t kInstalledRowHashOffset = 0;
/** FNV-1's basis, which this engine also uses as its absent-hash sentinel. */
constexpr std::uint32_t kAbsentNameHash = 0x811C9DC5U;

/**
 * Resolves the item behind a 904 that names no sale row.
 *
 * Amanda Holliday's Legacy Content tiles send `slot=1, row=-1`, so the slot is all that identifies
 * them - and it indexes the installed array: the Red War tile's vendor declares 220 sale rows but
 * 22 installed rows, and its slot is 1. That installed row carries the item's definition hash at
 * `+0`, where a sale row names its item by index. The resolution is logged either way, because a
 * wrong item that commits cleanly is harder to spot than a refusal.
 *
 * @param vendorIndex Vendor the request named.
 * @param slotIndex The 16-bit slot field, which is all the request carries.
 * @param itemDefinitionIndex Receives the item, or the unavailable sentinel.
 * @return True when the row's hash resolved to an installed item definition.
 */
[[nodiscard]] bool resolve_rowless_quest(std::int32_t vendorIndex,
                                         std::int32_t slotIndex,
                                         std::uint16_t& itemDefinitionIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    itemDefinitionIndex = kUnavailableDefinitionIndex;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (slotIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    vendor_domain::InstalledRow installed{};
    if (!vendor_domain::installed_row(definition, static_cast<std::size_t>(slotIndex), installed)) {
        return false;
    }
    const auto& raw = installed.raw;
    std::uint32_t definitionHash = 0;
    std::memcpy(&definitionHash, raw.data() + kInstalledRowHashOffset, sizeof definitionHash);

    state::build_data::items::Definition item{};
    const bool resolved = definitionHash != kAbsentNameHash
                          && state::build_data::find_item_definition_hash(definitionHash, item);
    if (resolved) {
        itemDefinitionIndex = item.definitionIndex;
    }
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=ws904 stage=rowless vendor=%d slot=%d installed=%u sale=%u "
                                "third=%u hash=0x%08X item=%u resolved=%u hex=",
                                static_cast<int>(vendorIndex),
                                static_cast<int>(slotIndex),
                                static_cast<unsigned>(definition.installedCount),
                                static_cast<unsigned>(definition.saleCount),
                                static_cast<unsigned>(definition.thirdCount),
                                definitionHash,
                                static_cast<unsigned>(itemDefinitionIndex),
                                resolved ? 1U : 0U);
    if (written > 0 && static_cast<std::size_t>(written) < line.size()) {
        std::size_t length = static_cast<std::size_t>(written);
        const auto* const bytes = reinterpret_cast<const std::byte*>(raw.data());
        (void)core::log::append_hex(line, length, {bytes, raw.size()});
        if (length != 0) {
            core::log::write(core::log::Channel::server,
                             resolved ? core::log::Level::info : core::log::Level::warn,
                             {line.data(), length});
        }
    }
    return resolved;
}

/** What one resolved vendor row turned out to be, once it was settled. */
enum class RowOutcome : std::uint8_t {
    /** The row rolled a bounty from an authored pool. */
    bountyRoll,
    /** The row charged one stack and credited others. */
    exchange,
    /** The row's item is prepared for the inventory; its offer is answered once that commits. */
    granted,
    /** The character already holds the row's pursuit, so its offer was answered some time ago. */
    alreadyHeld,
    /** The row should have granted and could not, so its offer still stands. */
    grantRefused,
};

/**
 * Whether Eva's Festival cards are live, judged by the single Courtyard key the character encoder
 * derives flags 20826/20829/20831 from (character_encoder.cpp). The claim gate and the banner gate
 * must be the same predicate, or a card the client was told to draw could be refused when clicked.
 */
[[nodiscard]] bool festival_of_the_lost_active() noexcept {
    namespace events = state::activity::events;
    events::ensure_loaded();
    return !events::withheld(0x7C6DE64FU);
}

[[nodiscard]] bool wearing_masks_source(bool eventActive,
                                        std::uint64_t& instanceSoid,
                                        std::size_t& inventoryIndex) noexcept {
    instanceSoid = 0;
    inventoryIndex = 0;
    const state::AccountState account = state::account_snapshot();
    const state::CharacterState* selected = nullptr;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (!account.characters[index].selected) {
            continue;
        }
        if (selected != nullptr) {
            return false;
        }
        selected = &account.characters[index];
    }
    if (selected == nullptr
        || !state::account::festival_quest::available(*selected, eventActive).wearingMasks) {
        return false;
    }
    for (std::size_t index = 0; index < selected->inventory.count; ++index) {
        const auto& item = selected->inventory.values[index];
        if (item.definitionHash != state::account::festival_quest::kSteps.front()) {
            continue;
        }
        if (instanceSoid != 0) {
            return false;
        }
        instanceSoid = item.instanceSoid;
        inventoryIndex = index;
    }
    return instanceSoid != 0;
}

/**
 * Settles one resolved vendor row, in the order a row's behaviours are tried.
 *
 * Both vendor opcodes end here. A row is a bounty roll, an exchange, or a grant, and which cannot
 * be read off the row itself: each is recognised by an authored rule keyed to the vendor, tried in
 * turn, and the first that claims the row owns it. One ordered chain is what keeps 901 and 904 from
 * drifting apart.
 *
 * @param message Request being answered.
 * @param opcode Opcode to report under.
 * @param vendorIndex Vendor the request names.
 * @param rowIndex Sale row the request names.
 * @param categoryIndex Category of that row, from sale row +100.
 * @param itemDefinitionIndex Item the row names.
 * @param outcome Receives whatever mutation the row prepared.
 * @return What the row turned out to be.
 */
RowOutcome settle_vendor_row(const middleware::web_service::Message& message,
                             std::uint16_t opcode,
                             std::int32_t vendorIndex,
                             std::int32_t rowIndex,
                             std::int32_t categoryIndex,
                             std::uint16_t itemDefinitionIndex,
                             Outcome& outcome) noexcept {
    state::build_data::items::Definition offeredDefinition{};
    const bool offeredResolved =
        state::build_data::find_item_definition_index(itemDefinitionIndex, offeredDefinition);
    state::build_data::vendors::IndexEntry offeredVendor{};
    const bool evaVendor = vendorIndex >= 0 && vendorIndex <= UINT16_MAX
        && state::build_data::vendors::find_index(static_cast<std::uint16_t>(vendorIndex), offeredVendor)
        && offeredVendor.definitionHash == festival_bags::kEva;
    if (evaVendor && offeredResolved && (offeredDefinition.definitionHash == kWerewolfRandomOffer
                            || offeredDefinition.definitionHash == kWerewolfWeapon)) {
        if (!prepare_festival_werewolf(vendorIndex, offeredDefinition.definitionHash, outcome)) {
            report_purchase(opcode, "fail", "werewolf_reward", vendorIndex, rowIndex, itemDefinitionIndex);
            return RowOutcome::grantRefused;
        }
        return RowOutcome::granted;
    }
    const bool maskOfferShape =
        opcode == state::account::festival_quest::kMaskReceiptOpcode
        && vendorIndex == state::account::festival_quest::kMaskReceiptVendor
        && categoryIndex == state::account::festival_quest::kMaskReceiptCategory
        && rowIndex >= state::account::festival_quest::kMaskReceiptFirstSale
        && rowIndex <= state::account::festival_quest::kMaskReceiptLastSale && offeredResolved;
    // Every Eva quest receipt logs its shape, matched or not, so a row that never enters the mask
    // branch (wrong category numbering, drifted item) is diagnosable from one line.
    if (opcode == state::account::festival_quest::kMaskReceiptOpcode
        && vendorIndex == state::account::festival_quest::kMaskReceiptVendor) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::info,
                          "ev=ws904 stage=eva_shape vendor=%d sale=%d category=%d item=%u "
                          "definition_hash=0x%08X resolved=%u mask_shape=%u",
                          static_cast<int>(vendorIndex),
                          static_cast<int>(rowIndex),
                          static_cast<int>(categoryIndex),
                          static_cast<unsigned>(itemDefinitionIndex),
                          offeredResolved ? offeredDefinition.definitionHash : 0U,
                          static_cast<unsigned>(offeredResolved),
                          static_cast<unsigned>(maskOfferShape));
    }
    if (maskOfferShape) {
        if (!state::account::festival_quest::is_mask_receipt(
                opcode, vendorIndex, categoryIndex, rowIndex, offeredDefinition.definitionHash)) {
            report_purchase(opcode, "fail", "mask_definition", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            outcome.mutation = std::monostate{};
            return RowOutcome::grantRefused;
        }
        const bool eventActive = festival_of_the_lost_active();
        std::uint64_t sourceInstanceSoid = 0;
        std::size_t sourceInventoryIndex = 0;
        if (!eventActive
            || !wearing_masks_source(eventActive, sourceInstanceSoid, sourceInventoryIndex)) {
            report_purchase(opcode, "fail", "wearing_masks_unavailable", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            outcome.mutation = std::monostate{};
            return RowOutcome::grantRefused;
        }
        // The mask itself is granted exactly as the ordinary chain below would grant it, collectible
        // and all, so the only difference on this row is the quest step riding the same transaction.
        std::uint16_t maskCollectibleIndex = state::build_data::collectibles::kNoCollectibleIndex;
        (void)find_collectible_for_item(itemDefinitionIndex, maskCollectibleIndex);
        if (grant_item_definition(message, maskCollectibleIndex, itemDefinitionIndex, outcome)
            != GrantResult::granted) {
            report_purchase(opcode, "fail", "mask_grant", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            outcome.mutation = std::monostate{};
            return RowOutcome::grantRefused;
        }
        auto* mutation = std::get_if<state::PendingItemAcquisition>(&outcome.mutation);
        if (mutation == nullptr
            || !state::stage_item_replacement(*mutation,
                                               sourceInstanceSoid,
                                               sourceInventoryIndex,
                                               state::account::festival_quest::kSteps.front(),
                                               state::account::festival_quest::kSteps[1])) {
            report_purchase(opcode, "fail", "quest_update", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            outcome.mutation = std::monostate{};
            return RowOutcome::grantRefused;
        }
        report_purchase(opcode, "ok", "mask_and_quest_ready", vendorIndex, rowIndex,
                        itemDefinitionIndex);
        return RowOutcome::granted;
    }
    if (offeredResolved && festival_bags::matches(offeredDefinition.definitionHash)) {
        const bool ready = prepare_festival_bag(vendorIndex, offeredDefinition.definitionHash, outcome);
        report_purchase(opcode, ready ? "ok" : "fail", "festival_bag", vendorIndex, rowIndex, itemDefinitionIndex);
        if (!ready) outcome.mutation = std::monostate{};
        return ready ? RowOutcome::granted : RowOutcome::grantRefused;
    }
    // The row's price is resolved once here and threaded into every grant below, so whatever a
    // row turns out to be, its charge lands inside the same prepared transaction as its item.
    //
    // It is resolved AFTER the mask-receipt branch has returned, which is deliberate: the
    // category-79 quest-delivery rows author no cost, and resolving the price below them means
    // they cannot reach the charge path at all - not even if a price were authored for one. The
    // accepted `Wearing Masks` -> `A Smiling Mask` advance stays exactly as it is today.
    VendorPrice price{};
    switch (price_for_vendor_row(vendorIndex, rowIndex, price)) {
    case PriceLookup::none:
    case PriceLookup::priced:
        break;
    case PriceLookup::broken:
        // A rule names this row but cannot be charged. Falling through would hand the row over
        // for nothing, which is the exact failure the price file exists to prevent.
        report_purchase(opcode, "fail", "price_unresolved", vendorIndex, rowIndex,
                        itemDefinitionIndex);
        outcome.mutation = std::monostate{};
        return RowOutcome::grantRefused;
    }
    const std::span<const state::build_data::material_requirements::Requirement> cost =
        price.charge();
    std::uint16_t rolledBounty = kUnavailableDefinitionIndex;
    if (roll_vendor_bounty(vendorIndex, categoryIndex, rolledBounty)) {
        if (rolledBounty == kUnavailableDefinitionIndex) {
            report_purchase(opcode, "ok", "bounty_pool_empty", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            return RowOutcome::bountyRoll;
        }
        std::uint16_t rolledCollectible = state::build_data::collectibles::kNoCollectibleIndex;
        (void)find_collectible_for_item(rolledBounty, rolledCollectible);
        // The grant result used to be discarded here, so an "Additional Bounties" row that could
        // not pay its authored Glimmer would report `ok` and then silently answer nothing. The
        // roll is reported by what the grant actually did.
        const GrantResult rolled =
            grant_item_definition(message, rolledCollectible, rolledBounty, outcome, cost);
        if (rolled == GrantResult::granted) {
            report_purchase(opcode, "ok", "bounty_roll", vendorIndex, rowIndex,
                            itemDefinitionIndex);
            return RowOutcome::bountyRoll;
        }
        // Only a refusal can be a refusal for cost. An `alreadyHeld` roll never reached the
        // charge, so naming a short balance there would diagnose the wrong thing.
        if (rolled == GrantResult::refused) {
            (void)report_price_refusal(opcode, vendorIndex, rowIndex, rolledBounty, price);
        }
        report_purchase(opcode,
                        "fail",
                        rolled == GrantResult::alreadyHeld ? "bounty_already_held" : "bounty_grant",
                        vendorIndex,
                        rowIndex,
                        itemDefinitionIndex);
        return RowOutcome::bountyRoll;
    }
    state::PendingProfileItemAcquisition exchange{};
    if (exchange_vendor_row(vendorIndex, rowIndex, exchange)) {
        // A row is an exchange or a sale, never both: an exchange claims the row before any grant
        // and charges its own authored cost, so a price rule on the same row would never be read.
        // Say so rather than let it disappear.
        if (price.count != 0) {
            core::log::writef(core::log::Channel::server,
                              core::log::Level::warn,
                              "ev=vendor_price stage=apply result=fail reason=exchange_row "
                              "vendor=%d row=%d costs=%zu",
                              vendorIndex,
                              rowIndex,
                              price.count);
        }
        report_purchase(opcode, "ok", "exchange", vendorIndex, rowIndex, itemDefinitionIndex);
        if (exchange.prepared) {
            outcome.mutation = exchange;
        }
        return RowOutcome::exchange;
    }
    // A placeholder row grants what it stands for, not the placeholder: a Dummy item put in the
    // Quests bucket is one the client will not draw, and the row never settles because the player
    // never receives what it offered.
    std::uint16_t granted = itemDefinitionIndex;
    std::uint16_t substituteIndex = kUnavailableDefinitionIndex;
    switch (substitute_for_item(granted, substituteIndex)) {
    case Substitution::replaced:
        granted = substituteIndex;
        break;
    case Substitution::broken:
        // The rule proves the row's item is a placeholder, so granting it would be the wrong
        // grant this path exists to prevent. The rule itself already logged what is missing.
        report_purchase(opcode, "fail", "substitute_missing", vendorIndex, rowIndex, granted);
        return RowOutcome::grantRefused;
    case Substitution::none:
        break;
    }
    std::uint16_t collectibleIndex = state::build_data::collectibles::kNoCollectibleIndex;
    const bool collected = find_collectible_for_item(granted, collectibleIndex);
    report_purchase(opcode,
                    "ok",
                    collected ? "resolved" : "resolved_no_collectible",
                    vendorIndex,
                    rowIndex,
                    granted);
    // A grant that failed for a transient reason - the loadout would not resolve, the bucket was
    // full - leaves the row's offer standing, and the caller must not treat it as answered.
    switch (grant_item_definition(message, collectibleIndex, granted, outcome, cost)) {
    case GrantResult::granted:
        return RowOutcome::granted;
    case GrantResult::alreadyHeld:
        return RowOutcome::alreadyHeld;
    case GrantResult::refused:
        break;
    }
    // Written only when a cost row is short at the time of the refusal, so `reason=price` is
    // never guessed. A grant refused before the charge was even reached - a missing bucket
    // descriptor, an item the build does not carry - reports its own reason and, unless the
    // balance also happens to be short, nothing else.
    (void)report_price_refusal(opcode, vendorIndex, rowIndex, granted, price);
    return RowOutcome::grantRefused;
}

namespace {

/** One qualified pickup report, remembered so a repeated report of the same drop pays once. */
struct LootClaim final {
    std::int32_t sourceTag{};
    std::uint64_t sourceHandle{};
    std::int32_t sequence{};
    std::uint64_t characterSoid{};
    bool occupied{};
};
std::mutex g_lootClaimMutex;
std::array<LootClaim, 128> g_lootClaims{};
std::size_t g_lootClaimCursor{};

[[nodiscard]] bool forest_destination_live() noexcept {
    const auto activity = state::activity::newest_joined_activity();
    if (!static_cast<bool>(activity) || !state::activity::contains(activity)) {
        return false;
    }
    state::activity::destination::DestinationSelection destination{};
    if (!state::activity::destination::snapshot(activity, destination)) {
        return false;
    }
    constexpr std::string_view forest = forest_loot::kForestPackageName;
    return destination.packageNameLength == forest.size()
           && std::equal(forest.begin(), forest.end(), destination.packageName.begin());
}

void report_loot(const char* stage,
                 const char* result,
                 const char* reason,
                 const middleware::web_service::messages::opcode601::Request& request,
                 std::uint64_t characterSoid) noexcept {
    core::log::writef(core::log::Channel::server,
                      std::strcmp(result, "ok") == 0 ? core::log::Level::info
                                                     : core::log::Level::warn,
                      "ev=ws601 stage=%s result=%s reason=%s kind=%d raw_kind=%u tag=%d "
                      "handle=0x%016llX sequence=%d raw_sequence=%u bits=%u consumed=%u exact=%u "
                      "character=0x%016llX",
                      stage,
                      result,
                      reason,
                      request.kind,
                      static_cast<unsigned>(request.rawKind),
                      request.sourceTag,
                      static_cast<unsigned long long>(request.sourceHandle),
                      request.sequence,
                      request.rawSequence,
                      request.payloadBits,
                      request.consumedBits,
                      request.exact ? 1U : 0U,
                      static_cast<unsigned long long>(characterSoid));
}

} // namespace

bool forest_loot::candy_drop_armed() noexcept {
    const bool festival = festival_of_the_lost_active();
    const state::AccountState account = state::account_snapshot();
    const std::uint64_t characterSoid = state::account::selected_character_soid(account);
    const bool mask = characterSoid != 0
                      && state::account::festival_mask::has_current_equipped_festival_mask(
                          characterSoid, account);
    const bool forest = forest_destination_live();
    std::uint32_t helmetHash{};
    std::uint64_t helmetSoid{};
    for (std::size_t i = 0; i < account.characterCount && i < account.characters.size(); ++i) {
        const auto& character = account.characters[i];
        if (character.soid != characterSoid) { continue; }
        const auto& helmet = character.equipment.slots[static_cast<std::size_t>(
            state::account::inventory::EquipmentSlot::helmet)];
        if (helmet) { helmetHash = helmet->definitionHash; helmetSoid = helmet->instanceSoid; }
    }
    // Game-thread caller; log gate and equipment changes without per-hit log spam.
    static std::uint64_t lastCharacter{}, lastHelmet{};
    static std::uint32_t lastHelmetHash{}, lastKey = UINT32_MAX;
    const std::uint32_t key = (festival ? 1U : 0U) | (mask ? 2U : 0U) | (forest ? 4U : 0U);
    if (key != lastKey || characterSoid != lastCharacter || helmetSoid != lastHelmet
        || helmetHash != lastHelmetHash) {
        lastKey = key;
        lastCharacter = characterSoid;
        lastHelmet = helmetSoid;
        lastHelmetHash = helmetHash;
        core::log::writef(core::log::Channel::server, core::log::Level::info,
                          "ev=forest_candy stage=armed festival=%u mask=%u forest=%u character=0x%016llX helmet=%08X helmet_soid=%016llX",
                          festival ? 1U : 0U, mask ? 1U : 0U, forest ? 1U : 0U,
                          static_cast<unsigned long long>(characterSoid), helmetHash,
                          static_cast<unsigned long long>(helmetSoid));
    }
    return festival && mask && forest;
}

namespace {
struct InjectedDrop final {
    std::int32_t tag{};
    std::uint64_t handle{};
    std::int32_t sequence{};
    std::uint32_t item{};
    std::int32_t quantity{1};
    bool occupied{};
};
std::mutex g_injectedDropMutex;
std::array<InjectedDrop, 256> g_injectedDrops{};
std::size_t g_injectedDropCursor{};
std::array<forest_loot::ChestDrop, 64> g_chestDrops{};
std::size_t g_chestDropCount{};
std::array<forest_loot::RetireRequest, 64> g_retires{};
std::size_t g_retireCount{};
std::uint64_t retire_now_ms() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

void forest_loot::request_retire(std::int32_t sourceTag, std::uint64_t sourceHandle,
                                 std::int32_t sequence) noexcept {
    const std::lock_guard lock(g_injectedDropMutex);
    if (g_retireCount < g_retires.size()) {
        g_retires[g_retireCount++] = {sourceTag, sourceHandle, sequence, retire_now_ms()};
    }
}

bool forest_loot::take_retire(RetireRequest& request, std::uint64_t minAgeMs) noexcept {
    const std::lock_guard lock(g_injectedDropMutex);
    const std::uint64_t now = retire_now_ms();
    for (std::size_t i = 0; i < g_retireCount; ++i) {
        if (now - g_retires[i].queuedTick < minAgeMs) {
            continue;
        }
        request = g_retires[i];
        g_retires[i] = g_retires[--g_retireCount];
        return true;
    }
    return false;
}

void forest_loot::remember_injected_drop(std::int32_t sourceTag, std::uint64_t sourceHandle,
                                         std::int32_t sequence, std::uint32_t itemDefinitionHash, std::int32_t quantity) noexcept {
    const std::lock_guard lock(g_injectedDropMutex);
    g_injectedDrops[g_injectedDropCursor] = {sourceTag, sourceHandle, sequence, itemDefinitionHash, quantity, true};
    g_injectedDropCursor = (g_injectedDropCursor + 1U) % g_injectedDrops.size();
}

bool forest_loot::injected_drop_item(std::int32_t sourceTag, std::uint64_t sourceHandle, std::int32_t sequence,
                                     std::uint32_t& itemDefinitionHash, std::int32_t& quantity) noexcept {
    const std::lock_guard lock(g_injectedDropMutex);
    for (const InjectedDrop& drop : g_injectedDrops) {
        if (drop.occupied && drop.tag == sourceTag && drop.handle == sourceHandle && drop.sequence == sequence) {
            itemDefinitionHash = drop.item;quantity=drop.quantity;
            return true;
        }
    }
    return false;
}

void forest_loot::finish_pickup(const PickupCommit& pickup, bool committed) noexcept {
    if (!pickup.prepared) { return; }
    if (committed) {
        {
            const std::lock_guard lock(g_lootClaimMutex);
            g_lootClaims[g_lootClaimCursor] = {pickup.sourceTag, pickup.sourceHandle,
                                               pickup.sequence, pickup.characterSoid, true};
            g_lootClaimCursor = (g_lootClaimCursor + 1U) % g_lootClaims.size();
        }
        request_retire(pickup.sourceTag, pickup.sourceHandle, pickup.sequence);
    }
    core::log::writef(core::log::Channel::server,
        committed ? core::log::Level::info : core::log::Level::warn,
        "ev=ws601 stage=pay result=%s reason=%s tag=%d handle=0x%016llX sequence=%d character=0x%016llX",
        committed ? "ok" : "fail",
        !committed ? "transaction_commit" : pickup.itemDefinitionHash == kChocolateStrangeCoinHash ? "coin" : "candy",
        pickup.sourceTag, static_cast<unsigned long long>(pickup.sourceHandle), pickup.sequence,
        static_cast<unsigned long long>(pickup.characterSoid));
}

void forest_loot::queue_chest_drops(std::int32_t count, float x, float y, float z,
    std::uint32_t itemDefinitionHash, std::int32_t quantity,
    state::activity::ActivityInstanceKey owner, std::uint32_t chestGeneration) noexcept {
    if (quantity<=0 || !static_cast<bool>(owner) || !chestGeneration) return;
    const std::lock_guard lock(g_injectedDropMutex);
    for (std::int32_t i = 0; i < count && g_chestDropCount < g_chestDrops.size(); ++i) {
        g_chestDrops[g_chestDropCount++] = {x, y, z, itemDefinitionHash, quantity,owner,chestGeneration};
    }
}

bool forest_loot::take_chest_drop(ChestDrop& drop) noexcept {
    const std::lock_guard lock(g_injectedDropMutex);
    if (g_chestDropCount == 0) {
        return false;
    }
    drop = g_chestDrops[--g_chestDropCount];
    return true;
}

/**
 * Answers one opcode-601 loot pickup report.
 *
 * The client rolls, records and spawns a dropped bauble itself from the killed object's reward
 * sheet, then reports the pickup with only the source reference and its own drop sequence. The
 * item is never on the wire, so this route decides the payout: one Candy for a bauble picked up in
 * the Haunted Forest by a character wearing a Festival mask while the Festival is live. Every
 * report is logged whether or not it pays, so a run that spawns no baubles or sends no reports can
 * be told apart from a refused one.
 */
void pickup_loot(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace loot = middleware::web_service::messages::opcode601;
    loot::Request request{};
    const state::AccountState account = state::account_snapshot();
    const std::uint64_t characterSoid = state::account::selected_character_soid(account);
    if (!loot::parse_request(message, request)) {
        report_loot("receive", "fail", "payload", request, characterSoid);
        return;
    }
    report_loot("receive", "ok", "parsed", request, characterSoid);
    if (!festival_of_the_lost_active()) {
        report_loot("qualify", "fail", "festival_withheld", request, characterSoid);
        return;
    }
    std::uint32_t itemHash = forest_loot::kCandyDefinitionHash;
    std::int32_t quantity=forest_loot::kCandyPerPickup;
    const bool registered = forest_loot::injected_drop_item(request.sourceTag, request.sourceHandle,
                                                            request.sequence, itemHash,quantity);
    if (characterSoid == 0
        || (!registered && !state::account::festival_mask::has_current_equipped_festival_mask(characterSoid,
                                                                              account))) {
        report_loot("qualify", "fail", "no_mask_equipped", request, characterSoid);
        return;
    }
    if (!forest_destination_live()) {
        report_loot("qualify", "fail", "not_in_forest", request, characterSoid);
        return;
    }
    if (request.kind != forest_loot::kRewardSheetDropKind
        && request.kind != forest_loot::kBaubleDropKindAlternate) {
        report_loot("qualify", "fail", "kind", request, characterSoid);
        return;
    }
    {
        const std::lock_guard lock(g_lootClaimMutex);
        for (const LootClaim& claim : g_lootClaims) {
            if (claim.occupied && claim.sourceTag == request.sourceTag
                && claim.sourceHandle == request.sourceHandle && claim.sequence == request.sequence
                && claim.characterSoid == characterSoid) {
                report_loot("qualify", "fail", "duplicate", request, characterSoid);
                return;
            }
        }
    }
    state::PendingProfileItemAcquisition mutation{};

    if (!state::prepare_profile_item_acquisition(state::build_data::collectibles::kNoCollectibleIndex,
                                                 itemHash,
                                                 mutation,{},quantity)) {
        report_loot("pay", "fail", "profile_state", request, characterSoid);
        return;
    }
    outcome.mutation = mutation;
    outcome.pickup = {request.sourceTag, request.sourceHandle, request.sequence,
                      characterSoid, itemHash, true};
    report_loot("prepare", "ok",
                itemHash == forest_loot::kChocolateStrangeCoinHash ? "coin"
                : registered ? "candy" : "candy_unregistered",
                request, characterSoid);
}

/**
 * Prepares one opcode-904 quest acquire.
 *
 * A quest names a vendor row exactly as a purchase does, and the item behind it is granted through
 * the same path, so a quest lands in the inventory the way a bounty now does.
 */
void acquire_quest(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace quest = middleware::web_service::messages::opcode904;
    quest::Request request{};
    if (!quest::parse_request(message, request)) {
        report_purchase(quest::kOpcode, "fail", "payload", -1, -1, kUnavailableDefinitionIndex);
        return;
    }
    // The 16-bit slot field is where the click landed, and indexing sale rows with it granted
    // armour mods. The 32-bit field is the real row; a body without one has never been captured,
    // and guessing the slot in as a sale row would reproduce that exact wrong grant - so it is
    // refused, and the refusal names the shape so a real capture can settle it.
    if (!request.hasSaleIndex) {
        report_purchase(quest::kOpcode,
                        "fail",
                        "sale_field_missing",
                        request.vendorIndex,
                        request.slotIndex,
                        kUnavailableDefinitionIndex);
        return;
    }
    const std::int32_t row = request.saleIndex;
    std::uint16_t itemDefinitionIndex = 0;
    const char* reason = "unknown";
    // A row of -1 is the client saying this tile is not a sale row at all, rather than a row that
    // failed to resolve, so it takes the installed array instead. Falling back to the slot as a
    // sale row would grant whatever sits there, which is the wrong-item bug that made quests hand
    // out armour mods.
    const bool rowless = row < 0;
    // A rowless 904 is an interaction reply rather than a purchase, and the rank-up reward tile is
    // one: its reply names no sale row, so the slot field is the interaction it answered.
    std::int32_t questCategoryIndex = -1;
    const bool located =
        rowless ? resolve_rowless_quest(request.vendorIndex, request.slotIndex, itemDefinitionIndex)
                : resolve_vendor_row(request.vendorIndex, row, itemDefinitionIndex,
                                    questCategoryIndex, reason);
    if (!located) {
        report_purchase(quest::kOpcode,
                        "fail",
                        rowless ? "rowless_unresolved" : reason,
                        request.vendorIndex,
                        row,
                        kUnavailableDefinitionIndex);
        // A tile that names no row grants nothing, so say what this vendor does offer that would
        // land in the Quests tab. That is the difference between "this click is broken" and "this
        // click was never a quest".
        if (rowless) {
            report_pursuit_rows(request.vendorIndex);
        }
        return;
    }
    const RowOutcome settled = settle_vendor_row(message,
                                                 quest::kOpcode,
                                                 request.vendorIndex,
                                                 row,
                                                 questCategoryIndex,
                                                 itemDefinitionIndex,
                                                 outcome);
    // The banner that offered this quest is answered only by a row whose offer is answered, and
    // nothing else tells the client so: its picker keeps choosing the same interaction for as long
    // as the quest is offerable. A bounty roll and an exchange leave the banner's own question
    // unanswered, and a refused grant still owes the player its quest.
    if (request.vendorIndex < 0
        || request.vendorIndex >= static_cast<std::int32_t>(state::vendors::kVendorCapacity)) {
        return;
    }
    const auto vendor = static_cast<std::uint16_t>(request.vendorIndex);
    switch (settled) {
    case RowOutcome::alreadyHeld:
        // Answered some time ago, and nothing is left to commit, so the banner retires now. This
        // is the re-click on a quest already in the tab.
        (void)state::vendors::answer_shown(vendor);
        break;
    case RowOutcome::granted:
        // Prepared, not committed. The answer rides the transaction and is written where the
        // grant commits, so a mutation dropped on the way never buries a quest still owed.
        outcome.answeredVendor = vendor;
        break;
    case RowOutcome::bountyRoll:
    case RowOutcome::exchange:
    case RowOutcome::grantRefused:
        break;
    }
}

/**
 * Prepares one opcode-901 vendor purchase, for any Tower vendor.
 *
 * The request names a vendor row and a sale row. The sale row names an item-definition index, which
 * is the same thing a Collections pull resolves its collectible to, so this resolves the row and
 * hands over to the very same grant.
 *
 * Cost is still not read off the sale row - its cost-bearing fields remain role-open on this
 * build - but it IS charged: `settle_vendor_row` resolves the row's authored price from
 * `vendor_price.txt` and hands it to the grant, so the charge and the item share one transaction
 * and an unaffordable row refuses instead of paying out. A row with no authored price keeps the
 * uncharged behaviour this build shipped with.
 */
void purchase_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace purchase = middleware::web_service::messages::opcode901;
    purchase::Request request{};
    if (!purchase::parse_request(message, request)) {
        report_purchase(purchase::kOpcode, "fail", "payload", -1, -1, kUnavailableDefinitionIndex);
        return;
    }
    std::uint16_t itemDefinitionIndex = 0;
    const char* reason = "unknown";
    std::int32_t categoryIndex = -1;
    if (!resolve_vendor_row(
            request.vendorIndex, request.saleIndex, itemDefinitionIndex, categoryIndex, reason)) {
        report_purchase(purchase::kOpcode,
                        "fail",
                        reason,
                        request.vendorIndex,
                        request.saleIndex,
                        kUnavailableDefinitionIndex);
        return;
    }
    // Bounties, quest steps and tokens carry no collectible. The acquisition takes the sentinel
    // rather than a made-up row, and both prepare and commit skip the collectible steps for it.
    (void)settle_vendor_row(message,
                            purchase::kOpcode,
                            request.vendorIndex,
                            request.saleIndex,
                            categoryIndex,
                            itemDefinitionIndex,
                            outcome);
}

} // namespace dawn::server::web_service
