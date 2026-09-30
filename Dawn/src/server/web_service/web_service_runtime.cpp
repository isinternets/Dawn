#include "web_service_runtime.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

#include "../../client/hooks/bootflow/internal.h"
#include "../../core/logging/log.h"
#include "../../middleware/encoding/bit_reader.h"
#include "../../middleware/encoding/byte_order.h"
#include "../../middleware/web_service/messages/opcode1901.h"
#include "../../middleware/web_service/messages/opcode205.h"
#include "../../middleware/web_service/messages/opcode206.h"
#include "../../middleware/web_service/messages/opcode501_codec.h"
#include "../../middleware/web_service/messages/opcode502.h"
#include "../../middleware/web_service/messages/opcode503.h"
#include "../../middleware/web_service/messages/opcode504.h"
#include "../../middleware/web_service/messages/opcode601/opcode601_codec.h"
#include "../../state/account/festival_mask.h"
#include "../../middleware/web_service/messages/opcode901/opcode901_codec.h"
#include "../../middleware/web_service/messages/opcode903.h"
#include "../../middleware/web_service/messages/opcode904/opcode904_codec.h"
#include "../../middleware/web_service/web_service_envelope.h"
#include "../../state/account/account_state.h"
#include "../../state/activity/events/activity_event_selection.h"
#include "../../state/build_data/runtime.h"
#include "../../state/runtime/runtime.h"
#include "opcode_routes.h"
#include "web_service_actions.h"
#include "settings_save.h"
#include "../../middleware/web_service/messages/opcode904.h"
#include "../../middleware/web_service/messages/opcode905.h"
#include "../../middleware/web_service/messages/opcode405.h"
#include "../../state/activity/runtime.h"

namespace dawn::server::web_service {

/** One ordinary event line carries an opcode and its fixed prefix. */
constexpr std::size_t kOpcodeLineCapacity = 64;
/** A request trace keeps enough payload to identify an item-action descriptor. */
constexpr std::size_t kRequestPayloadTraceBytes = 192;
/** Marks a trace that stopped at the cap, so a short hex string is not read as a short payload. */
constexpr std::string_view kTruncated = " truncated=1";
/** Web Service opcode used by the Character screen's Equip action. */
constexpr std::uint16_t kEquipOpcode = 403;
/** Web Service opcode used by the Character screen's Unequip action. */
constexpr std::uint16_t kUnequipOpcode = 404;
/** Web Service opcode used by item-state actions such as finisher Favorite. */
constexpr std::uint16_t kItemStateOpcode = 406;
/** Web Service opcode used by the Character screen's Dismantle action. */
constexpr std::uint16_t kItemDismantleOpcode = 402;
/** Web Service opcode used by Collections to create one item instance. */
constexpr std::uint16_t kItemAcquisitionOpcode = 1820;
/** The mutation variant's first alternative is the empty one, so index zero prepared nothing. */
constexpr std::size_t kNoMutation = 0;
/**
 * Logical status of a refused action. The descriptor biases logical zero to the wire success the
 * Client expects, so any other logical value reports a refusal. Its five bits hold no error
 * taxonomy, so one code covers every reason and the log line names the actual one.
 */
constexpr std::int32_t kRefusedStatus = 1;

/**
 * Logs the Web Service opcode and a bounded payload trace.
 * One svc-10 frame looks like any other, and the opcode drives the client's queuez state machine.
 * @param message Parsed request envelope and borrowed payload.
 */
void report_request(const middleware::web_service::Message& message) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=ws stage=request opcode=%u transaction=%u payload_bytes=%zu payload_hex=",
                      static_cast<unsigned>(message.opcode),
                      static_cast<unsigned>(message.transactionId),
                      message.payload.size());
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }

    std::size_t length = static_cast<std::size_t>(prefix);
    const std::size_t traced =
        (std::min)(message.payload.size(), static_cast<std::size_t>(kRequestPayloadTraceBytes));
    (void)core::log::append_hex(line, length, message.payload.first(traced));
    if (traced != message.payload.size() && length + kTruncated.size() < line.size()) {
        std::memcpy(line.data() + length, kTruncated.data(), kTruncated.size());
        length += kTruncated.size();
    }
    if (length != 0) {
        core::log::write(core::log::Channel::server, core::log::Level::info, {line.data(), length});
    }
}

/**
 * Reads the server's own clock for the purchase clock rule.
 * The system clock counts from the Unix epoch, which is the same base the request field uses.
 * @return Current time in Unix seconds.
 */
[[nodiscard]] std::int64_t server_clock_seconds() noexcept {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch).count();
}

/**
 * Answers a request whose own codec refused with the bare correlated echo.
 * The Client matches on the echoed transaction id. A missing body is worse than a thin one. It
 * under-runs the decoder and takes the BAP connection down.
 * @param message Parsed request whose correlation fields are echoed.
 * @param response Svc-11 response-body storage owned by the caller.
 * @param written Gets the encoded response-body size in bytes.
 * @return True when the echo fits.
 */
bool encode_echo(const middleware::web_service::Message& message,
                 std::span<std::byte> response,
                 std::size_t& written) noexcept {
    std::array<char, kOpcodeLineCapacity> line{};
    const int count = std::snprintf(
        line.data(), line.size(), "ev=ws stage=body result=echo opcode=%u", message.opcode);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    namespace ws = middleware::web_service;
    return ws::encode_response(
        message, ws::ResponseShape::generic, ws::StatusResponse{}, response, written);
}

/**
 * Parses and answers one Web Service request with its whole descriptor layout.
 * @param request Whole decrypted svc-10 body.
 * @param response Svc-11 response-body storage owned by the caller.
 * @param written Gets the encoded response-body size, or zero when the header does not parse.
 * @return False only when the envelope header does not parse.
 */
bool consume(std::span<const std::byte> request,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    Outcome outcome;
    return consume(request, response, written, outcome);
}

/**
 * Parses one request, prepares any action it names, and encodes the reply that reports it.
 * @param request Whole decrypted svc-10 body.
 * @param response Svc-11 response-body storage owned by the caller.
 * @param written Gets the encoded response-body size, or zero when the header does not parse.
 * @param outcome Gets the prepared action for the caller to publish, and is left empty when
 * the action was refused or the reply could not be encoded.
 * @return False only when the envelope header does not parse.
 */
bool consume(std::span<const std::byte> request,
             std::span<std::byte> response,
             std::size_t& written,
             Outcome& outcome) noexcept {
    written = 0;
    outcome = {};
    middleware::web_service::Message message;
    if (!middleware::web_service::parse_request(request, message)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws stage=parse result=fail");
        return false;
    }
    report_request(message);
    // A character created earlier is selected, and the Client's sign-in step leaves for the game
    // once its hold is off. This is the first request after that creation, so the Client has taken
    // in the account that names the selection by now.
    client::hooks::bootflow::apply_character_select_release();

    if(message.opcode==405) {
        middleware::web_service::messages::opcode405::Request serviceRequest{};state::vendors::Pending recovery;
        if(middleware::web_service::messages::opcode405::parse(message,serviceRequest) && serviceRequest.item>=0
            && state::vendors::prepare_recovery(serviceRequest.instance,static_cast<std::uint16_t>(serviceRequest.item),serviceRequest.quantity,recovery)) {
            outcome.mutation=std::move(recovery);
        }
        middleware::web_service::StatusResponse status{};status.code=1;
        return middleware::web_service::encode_response(message,middleware::web_service::ResponseShape::statusPair,status,response,written);
    }

    if(message.opcode==905) {
        middleware::web_service::messages::opcode905::Request serviceRequest{};state::vendors::Pending vendor;
        if(middleware::web_service::messages::opcode905::parse(message,serviceRequest) && (serviceRequest.location==1 || serviceRequest.location==2) && serviceRequest.item>=0
            && (!serviceRequest.hasClock || middleware::web_service::messages::opcode901::check_clock(
                {0,0,serviceRequest.clock,true},server_clock_seconds())==middleware::web_service::messages::opcode901::ClockPolicy::accepted)
            && state::vendors::prepare_decryption(serviceRequest.instance,static_cast<std::uint16_t>(serviceRequest.item),vendor,serviceRequest.location==2)) {
            outcome.mutation=std::move(vendor);
        }
        middleware::web_service::StatusResponse status{};status.code=1;
        return middleware::web_service::encode_response(message,middleware::web_service::ResponseShape::statusPair,status,response,written);
    }

    if(message.opcode==904) {
        namespace q=state::activity::newlight::launchpad::quest;
        middleware::web_service::messages::vendor_reply::Request reply{};
        middleware::web_service::StatusResponse status{};status.code=1;
        if(middleware::web_service::messages::vendor_reply::parse(message,reply)) {
            if (reply.vendor == state::account::festival_mask::kEvaVendorDefinitionIndex) {
                acquire_quest(message, outcome);
                status.code = outcome.mutation.index() != kNoMutation ? 0 : kRefusedStatus;
                return middleware::web_service::encode_response(message,
                    middleware::web_service::ResponseShape::statusPair,status,response,written);
            }
            const auto step=q::accepted_step(reply.vendor,reply.interaction,reply.reply,reply.selection);
            const auto run=state::activity::mission_run_generation();
            state::PendingNewlightQuest mutation{};
            if(step>=2 && run && q::towerRun.load()==run
                && state::activity::world_phase()==state::activity::WorldPhase::arrived
                && state::prepare_newlight_quest(static_cast<std::uint8_t>(step),mutation)) {
                outcome.mutation=mutation;
            } else if(step<0 && reply.vendor>=0 && reply.interaction>=0 && reply.reply>=0) {
                state::vendors::Pending vendor;
                if(state::vendors::prepare({static_cast<std::uint16_t>(reply.vendor),reply.selection,reply.interaction,reply.reply},vendor)) {
                    outcome.mutation=std::move(vendor);
                }
            }
        }
        // Success is encoded by the BAP publisher only after the matching Family-4
        // revision has been staged. Unsupported or duplicate replies remain refused.
        return middleware::web_service::encode_response(message,
            middleware::web_service::ResponseShape::statusPair,status,response,written);
    }

    if (message.opcode == middleware::web_service::messages::opcode701::kOpcode) {
        return save_settings(message, response, written);
    }

    if (message.opcode == middleware::web_service::messages::opcode205::kOpcode) {
        state::activity::events::ensure_loaded();
        const auto investment = state::investment_snapshot();
        return middleware::web_service::messages::opcode205::encode_response(
                   message, investment, response, written)
               || encode_echo(message, response, written);
    }

    if (message.opcode == middleware::web_service::messages::opcode503::kOpcode) {
        middleware::web_service::messages::opcode503::Request bootstrap;
        const bool parsed =
            middleware::web_service::messages::opcode503::parse_request(message, bootstrap);
        // The request's own key is echoed and adopted. An authored id here costs the ship and the
        // banner.
        if (!bootstrap.hasPrimarySoid) {
            bootstrap.primarySoid = state::account_snapshot().primarySoid;
        }
        state::activity::events::ensure_loaded();
        const auto investment = state::investment_snapshot();
        if (!parsed
            || !middleware::web_service::messages::opcode503::encode_response(
                message, bootstrap, investment, response, written)) {
            return encode_echo(message, response, written);
        }
        if (bootstrap.hasPrimarySoid && !state::set_primary_soid(bootstrap.primarySoid)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=ws503 stage=adopt result=fail");
        }
        return true;
    }

    if (message.opcode == middleware::web_service::messages::opcode501::kOpcode) {
        namespace opcode501 = middleware::web_service::messages::opcode501;
        // The reply names a SOID family three publishes. The roster is built from State, so a
        // created character is listed by the time the Client asks for it again. Its item records
        // come from Family 4, so the caller is also told to refresh that.
        std::uint64_t characterSoid = 0;
        opcode501::Request choices{};
        if (opcode501::parse_request(message, choices)
            && state::create_character(static_cast<state::CharacterRace>(choices.race),
                                       static_cast<state::CharacterGender>(choices.gender),
                                       static_cast<state::CharacterClass>(choices.characterClass),
                                       characterSoid)) {
            outcome.rosterChanged = true;
            client::hooks::bootflow::request_character_select_release();
        } else {
            // Nothing was created. State logs its own refusal, and a body that does not parse is
            // already in the request line. The reply still names an existing character's SOID.
            characterSoid = state::account::selected_character_soid(state::account_snapshot());
        }
        return opcode501::encode_response(message, characterSoid, response, written)
               || encode_echo(message, response, written);
    }

    // A subscribe whose body does not parse is still answered; only the subscription is dropped.
    middleware::queuez::Subscription subscription;
    const bool subscribes =
        message.opcode == middleware::web_service::messages::opcode206::kOpcode
        && middleware::web_service::messages::opcode206::parse_request(message, subscription);

    // The action runs before its reply is encoded, because the reply reports whether it worked.
    // Each action fills the outcome only once it has prepared its whole transition, so an outcome
    // still empty after one ran is that action refusing the request. Nothing is published here:
    // both the prepared mutation and the subscription are handed back for the caller to publish
    // once the whole response is framed.
    bool dispatched = true;
    if (message.opcode == middleware::web_service::messages::opcode504::kOpcode) {
        select_character(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode502::kOpcode) {
        delete_character(message, outcome);
    } else if (message.opcode == kItemDismantleOpcode) {
        dismantle_item(message, outcome);
    } else if (message.opcode == kEquipOpcode) {
        mutate_equipment(message, false, outcome);
    } else if (message.opcode == kUnequipOpcode) {
        mutate_equipment(message, true, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode903::kOpcode) {
        mutate_socket_plug(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode1901::kOpcode) {
        mutate_equipped_socket_plug(message, outcome);
    } else if (message.opcode == kItemStateOpcode) {
        mutate_item_state(message, outcome);
    } else if (message.opcode == kItemAcquisitionOpcode) {
        acquire_item(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode901::kOpcode) {
        namespace codec=middleware::web_service::messages::opcode901;
        codec::Request purchase{};
        if (codec::parse_request(message,purchase) && purchase.vendorIndex>=0 && purchase.saleIndex>=0
            && (!purchase.hasClock || codec::check_clock(purchase,server_clock_seconds())==codec::ClockPolicy::accepted)) {
            if (purchase.vendorIndex == state::account::festival_mask::kEvaVendorDefinitionIndex) {
                purchase_item(message, outcome);
            } else {
                state::vendors::Pending vendor;
                if(state::vendors::prepare({static_cast<std::uint16_t>(purchase.vendorIndex),purchase.saleIndex,-1,0},vendor))
                    outcome.mutation=std::move(vendor);
                // Vendor service success is published only with its Family-4 commit.
                middleware::web_service::StatusResponse refused{};refused.code=kRefusedStatus;
                return middleware::web_service::encode_response(message,
                    middleware::web_service::ResponseShape::statusPairWithBool,refused,response,written);
            }
        }
    } else if (message.opcode == middleware::web_service::messages::opcode904::kOpcode) {
        acquire_quest(message, outcome);
    } else if (message.opcode == middleware::web_service::messages::opcode601::kOpcode) {
        pickup_loot(message, outcome);
    } else {
        dispatched = false;
    }
    const bool prepared = outcome.hasSelectedCharacter || outcome.rosterChanged
                          || outcome.mutation.index() != kNoMutation;

    middleware::web_service::ResponseShape shape{};
    resolve_response_shape(message.opcode, shape);
    middleware::web_service::StatusResponse status{};
    // A pickup the server does not pay keeps the historical neutral answer: what the client does
    // with a refused pickup status is not established, and the bauble is its own to reap.
    if (dispatched && !prepared
        && message.opcode != middleware::web_service::messages::opcode601::kOpcode) {
        status.code = kRefusedStatus;
    }
    if (!middleware::web_service::encode_response(message, shape, status, response, written)) {
        // The echo carries no status, so nothing may be published against it.
        outcome = {};
        return encode_echo(message, response, written);
    }
    if (subscribes) {
        // Publish the subscription only after its correlated response is complete.
        outcome.hasSubscription = true;
        outcome.subscription = subscription;
    }
    return true;
}

} // namespace dawn::server::web_service
