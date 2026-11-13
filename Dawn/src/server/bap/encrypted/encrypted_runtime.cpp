#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../middleware/secure_channel/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../internal.h"
#include "activity_transaction/activity_transaction_notifications.h"
#include "bap_connection_publication.h"
#include "internal.h"
#include "push/activity/activity_roster_push.h"
#include "queuez/queuez_outcome_staging.h"
#include "transactions/service_outcome_commit.h"

namespace dawn::server::bap::encrypted {
namespace {

/**
 * Wipes the part of one scratch buffer that may hold written bytes.
 * @param buffer Lock-owned scratch storage.
 * @param size Largest prefix that may hold transformed bytes.
 */
void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}

} // namespace

/**
 * Authenticates and answers one supported encrypted post-bootstrap request.
 * @param session Connection-owned authentication and nonce state.
 * @param scratch Lock-owned transform buffers kept off the Client thread stack.
 * @param outer Validated encrypted outer frame.
 * @param response Caller-owned complete-frame storage.
 * @param written Receives encoded response bytes.
 * @return True when routing succeeds and any response fits, commits State, and publishes its nonce.
 */
bool consume(Session& session,
             Scratch& scratch,
             const middleware::bap::OuterFrame& outer,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    written = 0;
    session.accountMutationPublished = false;
    session.accountResyncSelf = false;
    if (!session.authenticated) {
        // Staying silent here looks the same as a decode fault, and both look like a dead link.
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap stage=encrypted result=drop reason=unauthenticated");
        return false;
    }

    std::size_t plaintextSize = 0;
    const auto& bapState = state::bap();
    if (!middleware::secure_channel::open_frame(bapState.sessionKey,
                                                session.receiveNonce,
                                                outer.payload,
                                                scratch.plaintext,
                                                plaintextSize)) {
        const std::size_t possiblePlaintextSize =
            outer.payload.size() >= middleware::secure_channel::kFrameTagSize
                ? outer.payload.size() - middleware::secure_channel::kFrameTagSize
                : 0;
        clear_prefix(scratch.plaintext, possiblePlaintextSize);
        // The service is unreadable while the frame is sealed, so this line names no service.
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=decrypt result=fail");
        return false;
    }
    // Authentication consumes the receive nonce even when the inner service is unsupported.
    middleware::secure_channel::advance_nonce(session.receiveNonce);

    middleware::bap::RequestFrame frame;
    ServiceRoute route;
    std::size_t responseBodySize = 0;
    std::size_t framedSize = 0;
    ServiceOutcome outcome{};
    transactions::Publication publication{};
    state::activity::BindingKey stagedActivityBinding{};
    gameplay::group::HostActivityLineageLease bindingLineageLease{};
    queuez::SessionState nextQueuez = session.queuez;
    bool publishesQueuez = false;
    bool handled =
        middleware::bap::parse_request_payload(std::span(scratch.plaintext).first(plaintextSize),
                                               middleware::bap::FrameType::encrypted,
                                               frame)
        && routing::resolve(frame.messageId, route);
    if (!handled) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=parse result=fail");
    }
    const bool processesBody = handled && route.responseMode != ResponseMode::none;
    const bool sendsReply = handled && route.responseMode == ResponseMode::reply;
    // Pure one-way services consume only the authenticated receive nonce.
    if (processesBody
        && !body::process(route,
                          session,
                          frame.body,
                          scratch.responseBody,
                          responseBodySize,
                          outcome)) {
        diagnostics::report_failure(frame.messageId, "body");
        // A reply-mode service answers with an empty body instead of not at all. The Client
        // matches only the head of its pending ring, so one unanswered request jams that ring for
        // good and every later reply is rejected, which is worse than a thin reply.
        clear_prefix(scratch.responseBody, responseBodySize);
        responseBodySize = 0;
        outcome = {};
        handled = sendsReply;
    }
    if (handled && sendsReply) {
        handled = reply::encode(scratch,
                                route,
                                frame.taskId,
                                bapState.sessionKey,
                                session.sendNonce,
                                std::span(scratch.responseBody).first(responseBodySize),
                                framedSize);
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "encode");
        }
    }
    // Stage every requested frame and check for caller room before committing State or the nonce.
    auto nextSendNonce = session.sendNonce;
    if (handled && sendsReply) {
        middleware::secure_channel::advance_nonce(nextSendNonce);
    }
    queuez::StagedPublication queuezPublication{};
    if (handled) {
        handled = queuez::stage_service_outcome(scratch,
                                                session.queuez,
                                                outcome,
                                                bapState.sessionKey,
                                                nextSendNonce,
                                                scratch.framed,
                                                framedSize,
                                                queuezPublication);
        if (handled && queuezPublication.hasState) {
            nextQueuez = queuezPublication.after;
            publishesQueuez = true;
        }
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "stage");
        }
    }
    const auto* activityPlan = transaction_if<activity_message::ActivityPlan>(outcome);
    activity_transaction::NotificationStaging notificationStaging{};
    if (handled && activityPlan != nullptr) {
        handled = route.responseMode == ResponseMode::uncorrelatedPush;
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "route");
        } else {
            const activity_transaction::NotificationStageResult staged =
                activity_transaction::stage_notifications(session,
                                                          scratch,
                                                          *activityPlan,
                                                          bapState.sessionKey,
                                                          nextSendNonce,
                                                          scratch.framed,
                                                          framedSize,
                                                          notificationStaging);
            if (staged == activity_transaction::NotificationStageResult::failed) {
            diagnostics::report_failure(frame.messageId, "notify");
                handled = false;
            }
        }
    }
    if (handled && processesBody) {
        handled = transactions::prepare_publication(outcome, publication);
        if (handled && publication.hasActivityBinding) {
            handled = lifecycle::stage_activity_binding_key(session, stagedActivityBinding);
            if (handled) {
                handled = prepare_region_lineage_locked(session,
                                                        publication.activity,
                                                        publication.activityBindingFromJoin,
                                                        publication.regionLineage,
                                                        bindingLineageLease);
                publication.hasRegionLineage = handled;
            }
        }
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "binding");
        }
    }
    const bool mutatesAccount =
        outcome.hasChangeCharacter || outcome.hasSelectCharacter
        || transaction_if<EquipmentSwapTransaction>(outcome) != nullptr
        || transaction_if<SocketPlugTransaction>(outcome) != nullptr
        || transaction_if<ItemStateTransaction>(outcome) != nullptr
        || transaction_if<VendorServiceTransaction>(outcome) != nullptr
        || transaction_if<NewlightQuestTransaction>(outcome) != nullptr
        || transaction_if<ItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ProfileItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ItemDismantleTransaction>(outcome) != nullptr;
    // State commits consume and clear their pending payloads. Retain only the small diagnostic
    // fields needed after publication; QueueZ after-images stay owned by the transaction variant.
    const auto* stagedSocket = transaction_if<SocketPlugTransaction>(outcome);
    const std::uint8_t socketLane = stagedSocket == nullptr ? 0 : stagedSocket->pending.socketLane;
    const std::uint16_t socketPlugDefinition =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.plugDefinitionIndex;
    const std::uint8_t socketTargetBucket =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.targetBucketId;
    const std::uint8_t socketPlugBucket =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.plugBucketId;
    const auto* stagedItemState = transaction_if<ItemStateTransaction>(outcome);
    const std::uint64_t itemStateInstance =
        stagedItemState == nullptr ? 0 : stagedItemState->pending.targetInstanceSoid;
    const std::uint32_t itemStateFlags =
        stagedItemState == nullptr ? 0 : stagedItemState->pending.afterFlags;
    const auto* stagedProfile = transaction_if<ProfileItemAcquisitionTransaction>(outcome);
    const std::uint32_t profileDefinitionHash =
        stagedProfile == nullptr ? 0 : stagedProfile->pending.acquiredDefinitionHash;
    const std::int32_t profileQuantity =
        stagedProfile == nullptr ? 0 : stagedProfile->pending.acquiredQuantity;
    const bool profileActionSource =
        stagedProfile != nullptr && stagedProfile->pending.actionSource;
    const bool profileAppended = stagedProfile != nullptr && stagedProfile->pending.appended;
    // Committing the transaction clears the mutation the member key lives in, so the connection
    // fields are captured before the commit and published after it.
    const ConnectionFields connection = connection_fields(outcome);
    if (handled && processesBody) {
        // Recheck the staged binding immediately before commit. An atomic replacement cannot be
        // rolled back to its predecessor, so every connection-side publication precondition must
        // already be guaranteed before State overwrites that slot.
        handled = can_publish_connection_fields(session, stagedActivityBinding, publication);
        // State changes become visible only after every requested frame and caller byte fit.
        const bool pinnedAdvertisementCurrent =
            !notificationStaging.advertisementLease.pinned
            || gameplay::group::validate_host_activity_lineage(
                notificationStaging.advertisementLease);
        const bool bindingLineageCurrent =
            !bindingLineageLease.pinned
            || gameplay::group::validate_host_activity_lineage(bindingLineageLease);
        const bool regionLineageCurrent =
            !notificationStaging.regionBundle
            || retained_region_lineage_is_current_locked(
                session,
                session.activity.lineage,
                notificationStaging.boundLineageLease);
        bool regionSourceCurrent = true;
        if (notificationStaging.result
                == activity_transaction::NotificationStageResult::complete
            && notificationStaging.regionBundle
            && requires_notification(notificationStaging.snapshot.required,
                                     RegionNotification::roster)) {
            const auto& snapshot = notificationStaging.snapshot;
            const bool pendingAuthoritativeHost =
                activityPlan != nullptr && activityPlan->regionMoved
                && snapshot.expectedHostRegion
                       == activityPlan->membershipMutation.regionTransition.expectedHostRegion
                && snapshot.sourceHostRegion
                       == activityPlan->membershipMutation.regionTransition.nextHostRegion
                && snapshot.expectedHostRegion != snapshot.sourceHostRegion;
            if (!pendingAuthoritativeHost) {
                state::activity::membership::RegionView source{};
                regionSourceCurrent = state::activity::membership::snapshot_region_view(
                    snapshot.regionSource, snapshot.sourceHostRegion, source);
            }
        }
        handled = handled && pinnedAdvertisementCurrent && bindingLineageCurrent
                  && regionLineageCurrent && regionSourceCurrent
                  && framedSize <= response.size()
                  && transactions::commit(outcome, publication);
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "commit");
        }
        if (handled) {
            if (notificationStaging.result
                == activity_transaction::NotificationStageResult::deferred) {
                session.activity.regionDebt = notificationStaging.debt;
            }
            std::copy_n(scratch.framed.begin(), framedSize, response.begin());
            written = framedSize;
            // A fresh allocation is unowned between its State commit and this publication. Keep
            // an exact post-commit guard until the bytes and typed BAP lease are both published.
            const bool connectionPublished = publish_connection_fields(
                session, stagedActivityBinding, publication, connection);
            if (!connectionPublished) {
                const state::activity::ActivityInstanceKey rollback =
                    transactions::rollback_activity(publication);
                if (static_cast<bool>(rollback)) {
                    static_cast<void>(state::activity::retire_session_exact(rollback));
                }
                SecureZeroMemory(response.data(), written);
                written = 0;
                handled = false;
                diagnostics::report_failure(frame.messageId, "publish");
            }
            if (handled) {
                // The caller copy and binding publication finish before the nonce and staged
                // connection-local after-images advance.
                session.sendNonce = nextSendNonce;
                if (publishesQueuez) {
                    session.queuez = nextQueuez;
                }
                arm_repushes(session, queuezPublication);
                if (notificationStaging.result
                    == activity_transaction::NotificationStageResult::complete) {
                    push::activity::commit_staged_roster(session);
                    if (notificationStaging.regionBundle
                        && requires_notification(notificationStaging.snapshot.required,
                                                 RegionNotification::membership)) {
                        session.activity.advertisedRegion =
                            notificationStaging.snapshot.regionIndex;
                    }
                    if (notificationStaging.regionBundle
                        && notificationStaging.snapshot.publishesHud) {
                        publish_hud_region_locked(
                            session, notificationStaging.snapshot.sourceHostRegion);
                    }
                    if (session.activity.regionDebt.present
                        && session.activity.regionDebt.binding
                               == notificationStaging.snapshot.binding
                        && session.activity.regionDebt.committedHostRegion
                               == notificationStaging.snapshot.sourceHostRegion) {
                        session.activity.regionDebt = {};
                    }
                }
            }
            if (handled) {
                // A created or deleted character reaches the other peers like any account change,
                // and its own peer is refreshed too, because no update of its own rides in this
                // reply.
                session.accountMutationPublished = mutatesAccount || outcome.rosterChanged;
                session.accountResyncSelf = outcome.rosterChanged;
            if (transaction_if<EquipmentSwapTransaction>(outcome) != nullptr) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=equip stage=output_publish result=ok framed_bytes=%zu queuez_published=%u "
                    "family_version=%d family0_version=%d family3_version=%d",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    session.queuez.family0Version,
                    session.queuez.family3Version);
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<SocketPlugTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=socket_plug stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d family0_version=%d "
                    "family3_version=%d instance=0x%llX lane=%u "
                    "plug_definition=%u target_bucket=%u plug_bucket=%u",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    session.queuez.family0Version,
                    session.queuez.family3Version,
                    static_cast<unsigned long long>(transaction->update.targetInstanceSoid),
                    static_cast<unsigned>(socketLane),
                    static_cast<unsigned>(socketPlugDefinition),
                    static_cast<unsigned>(socketTargetBucket),
                    static_cast<unsigned>(socketPlugBucket));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemStateTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=item_state stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d instance=0x%llX flags=0x%X",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned long long>(itemStateInstance),
                    itemStateFlags);
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemAcquisitionTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=acquire stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u instance=0x%llX",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    static_cast<unsigned long long>(transaction->update.acquiredInstanceSoid));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction =
                    transaction_if<ProfileItemAcquisitionTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=profile_acquire stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u definition_hash=0x%08X "
                    "quantity=%d instance=0x%llX action_source=%u appended_row=%u "
                    "appended_resident=%u",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    profileDefinitionHash,
                    profileQuantity,
                    static_cast<unsigned long long>(transaction->update.acquiredInstanceSoid),
                    static_cast<unsigned>(profileActionSource),
                    static_cast<unsigned>(profileAppended),
                    static_cast<unsigned>(transaction->update.appendedResident));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemDismantleTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=dismantle stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u instance=0x%llX",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    static_cast<unsigned long long>(transaction->update.dismantledInstanceSoid));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            }
        }
    }
    if (!handled) {
        activity_transaction::discard_notification_staging(session, notificationStaging);
    }
    gameplay::group::release_host_activity_lineage(
        notificationStaging.advertisementLease);
    gameplay::group::release_host_activity_lineage(
        notificationStaging.boundLineageLease);
    gameplay::group::release_host_activity_lineage(bindingLineageLease);
    clear_prefix(scratch.plaintext, plaintextSize);
    clear_prefix(scratch.responseBody, responseBodySize);
    clear_prefix(scratch.framed, framedSize);
    outcome = {};
    SecureZeroMemory(&publication, sizeof publication);
    SecureZeroMemory(&queuezPublication, sizeof queuezPublication);
    if (handled) {
        core::log::write(core::log::Channel::server, core::log::Level::info, route.successEvent);
    }
    return handled;
}

} // namespace dawn::server::bap::encrypted
