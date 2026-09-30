#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/secure_channel/runtime.h"
#include "../../../../state/account/account_state.h"
#include "../../../../state/runtime/runtime.h"
#include "../internal.h"
#include "../activity_message/festival_pickups.h"
#include "../activity_message/forest_chest_rewards.h"
#include "../activity_message/forest_loot_pickups.h"
#include "../push/activity/activity_keepalive_push.h"
#include "../push/activity/launchpad_inventory.h"
#include "../push/activity/homecoming_inventory.h"
#include "../push/activity/adieu_inventory.h"
#include "../push/activity/newlight_quest.h"
#include "../push/activity/quest_progress.h"
#include "../../../../state/activity/nightfall/rules.h"
#include "../../../../state/activity/nightfall/completion_reward.h"
#include "queuez_state_validation.h"
#include "../activity_message/lost_sector_rewards.h"

namespace dawn::server::bap::encrypted {
namespace {

/** Widest re-push report, sized for the fields below. */
constexpr std::size_t kRepushReportLimit = 96;

/** Publishes and commits one exact Dawn-authored Nightfall currency debt. */
[[nodiscard]] bool consume_completion_reward(Session& session,
                                             Scratch& scratch,
                                             std::span<std::byte> response,
                                             std::size_t& written,
                                             bool& touchesScratch) noexcept {
    namespace reward = state::activity::nightfall::rewards;
    if (!static_cast<bool>(session.activity.instance) || !session.queuez.family4Active) {
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    if (!reward::account_matches(account.primarySoid, session.queuez.family4RootSoid)) {
        return false;
    }
    reward::Ticket ticket{};
    if (!reward::claim(session.activity.instance.sessionId, account.primarySoid, ticket)) {
        return false;
    }
    state::PendingProfileItemAcquisition mutation{};
    const auto prepared =
        state::prepare_profile_currency_grant(ticket.definitionHash, ticket.quantity, mutation);
    if (prepared == state::ProfileCurrencyGrantResult::capped) {
        if (!reward::finish_durable(ticket, 0)) {
            reward::release(ticket);
            return false;
        }
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=nightfall_reward result=ok credited=0 reason=currency_cap");
        return false;
    }
    if (prepared != state::ProfileCurrencyGrantResult::prepared) {
        reward::release(ticket);
        return false;
    }

    queuez::ProfileItemAcquisition acquisition{};
    if (mutation.accountSoid != session.queuez.family4RootSoid
        || !queuez::stage_profile_item_acquisition(session.queuez,
                                                   mutation.accountSoid,
                                                   mutation.acquiredInstanceSoid,
                                                   mutation.actionSource,
                                                   mutation.appended,
                                                   acquisition)) {
        reward::release(ticket);
        return false;
    }
    touchesScratch = true;
    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    if (!push::append_profile_item_acquisition_notification(scratch,
                                                            acquisition,
                                                            mutation,
                                                            state::bap().sessionKey,
                                                            nextSendNonce,
                                                            scratch.framed,
                                                            framedSize)
        || framedSize == 0 || framedSize > response.size()) {
        reward::release(ticket);
        return false;
    }
    middleware::secure_channel::advance_nonce(nextSendNonce);
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    const std::int32_t credited = mutation.acquiredQuantity - mutation.previousQuantity;
    if (credited <= 0 || credited > ticket.quantity
        || !state::commit_profile_item_reward(mutation, ticket.debtId, credited)) {
        reward::release(ticket);
        return false;
    }
    session.sendNonce = nextSendNonce;
    session.queuez = acquisition.after;
    session.accountMutationPublished = true;
    written = framedSize;
    (void)reward::finish(ticket, credited);
    std::array<char, kRepushReportLimit> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=nightfall_reward result=ok run=%llu credited=%d bytes=%zu",
                                    static_cast<unsigned long long>(ticket.run),
                                    credited,
                                    framedSize);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return true;
}

/**
 * Logs one delayed re-push with its framed size, so it can be compared to the first copy.
 * @param stage Point in the deferred push the line reports.
 * @param bytes Framed size of the published notification.
 */
void report_repush(const char* stage, std::size_t bytes) noexcept {
    std::array<char, kRepushReportLimit> line{};
    const int count = std::snprintf(
        line.data(), line.size(), "ev=queuez stage=%s result=ok bytes=%zu", stage, bytes);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** Publishes the current account graph after account or native power state invalidates it. */
[[nodiscard]] bool consume_account_resync(Session& session,
                                          Scratch& scratch,
                                          std::span<std::byte> response,
                                          std::size_t& written,
                                          bool& touchesScratch) noexcept {
    const bool accountPending =
        session.accountResyncArmed && session.accountResyncGeneration != 0;
    const std::uint64_t powerRevision = state::activity::nightfall::power_revision();
    if ((powerRevision & 1U) != 0) {
        return false;
    }
    const bool powerPending = session.nightfallPowerRevision != powerRevision;
    if (!accountPending && !powerPending) {
        return false;
    }
    // A power edge can precede the account subscription. Its initial snapshot already reads the
    // current projection, and this mismatch remains available for a later versioned refresh.
    if (!accountPending && !session.queuez.family4Active) {
        return false;
    }
    touchesScratch = true;
    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState currentQueuez{};
    if (!push::append_account_resync_notification(scratch,
                                                  session.queuez,
                                                  state::bap().sessionKey,
                                                  nextSendNonce,
                                                  scratch.framed,
                                                  framedSize,
                                                  currentQueuez)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=peer_resync result=fail reason=family4");
        return false;
    }
    if (currentQueuez.family0Active) {
        queuez::SessionState appearanceAfter{};
        if (!push::append_account_resync_appearance_notification(scratch,
                                                                 currentQueuez,
                                                                 state::bap().sessionKey,
                                                                 nextSendNonce,
                                                                 scratch.framed,
                                                                 framedSize,
                                                                 appearanceAfter)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync result=fail reason=family0");
            return false;
        }
        currentQueuez = appearanceAfter;
    }
    if (currentQueuez.family3Active) {
        queuez::SessionState rosterAfter{};
        if (!push::append_account_resync_roster_notification(scratch,
                                                             currentQueuez,
                                                             state::bap().sessionKey,
                                                             nextSendNonce,
                                                             scratch.framed,
                                                             framedSize,
                                                             rosterAfter)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync result=fail reason=family3");
            return false;
        }
        currentQueuez = rosterAfter;
    }
    // The three family builders read the lock-free projection independently. Reject the entire
    // staged bundle if launch or orbit changed it at any point, so one peer never observes mixed
    // capped and uncapped records from a single refresh.
    const std::uint64_t finalPowerRevision = state::activity::nightfall::power_revision();
    if ((finalPowerRevision & 1U) != 0 || finalPowerRevision != powerRevision) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=power_resync result=retry reason=revision");
        return false;
    }
    if (framedSize == 0 || framedSize > response.size() || !queuez::valid(currentQueuez)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=peer_resync result=fail reason=output");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    session.queuez = currentQueuez;
    if (accountPending) {
        session.accountGeneration = session.accountResyncGeneration;
        session.accountResyncArmed = false;
    }
    // The matching even revision proves that every builder saw one complete cap publication.
    session.nightfallPowerRevision = powerRevision;
    report_repush(accountPending ? "peer_resync" : "power_resync", framedSize);
    return true;
}

/**
 * Sends the owed banner re-push once its delay has passed.
 * The banner has no subscribe of its own, so the timer is its only second chance.
 * @param session Auth, nonce and queuez state owned by the connection.
 * @param scratch Transform buffers owned by the lock.
 * @param response Whole-frame storage owned by the caller.
 * @param written Gets the encoded notification size in bytes.
 * @param touchesScratch Set before any scratch buffer is used.
 * @return True when a whole banner notification is published.
 */
[[nodiscard]] bool consume_banner_repush(Session& session,
                                         Scratch& scratch,
                                         std::span<std::byte> response,
                                         std::size_t& written,
                                         bool& touchesScratch) noexcept {
    if (!session.bannerRepushArmed || session.bannerRepushRoot == 0
        || GetTickCount64() < session.bannerRepushDueTick) {
        return false;
    }
    // Nothing is owed while the account owns no character to name. The arm stays set, because it
    // is the banner's only second chance.
    if (state::account::banner_character_soid(state::banner_account_snapshot()) == 0) {
        return false;
    }
    touchesScratch = true;

    // The same body the subscribe answer builds, so the version and this host's mirror stay in
    // step. `append_banner_notification` fixes the version at zero and a pick has moved past it.
    middleware::queuez::Subscription subscription{};
    subscription.familyType = queuez::kBannerFamilyType;
    subscription.familyRootSoid = session.bannerRepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState bannerAfter{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     state::bap().sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     bannerAfter,
                                     armsRepush,
                                     armsBannerRepush);
    if (framedSize == 0 || framedSize > response.size()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=banner_repush result=fail");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    // The frame is committed here, so the recorded delivery and the arm are committed with it.
    if (valid(bannerAfter)) {
        session.queuez = bannerAfter;
    }
    session.bannerRepushArmed = false;
    report_repush("banner_repush", framedSize);
    return true;
}

} // namespace

/**
 * Sends the owed Family-4 re-push once its delay has passed.
 * @param session Auth, nonce and queuez state owned by the connection.
 * @param scratch Transform buffers owned by the lock.
 * @param response Whole-frame storage owned by the caller.
 * @param written Gets the encoded notification size in bytes.
 * @param touchesScratch Set before any scratch buffer is used.
 * @return True when a whole Family-4 notification is published.
 */
bool consume_deferred(Session& session,
                      Scratch& scratch,
                      std::span<std::byte> response,
                      std::size_t& written,
                      bool& touchesScratch) noexcept {
    written = 0;
    if (!session.authenticated) {
        return false;
    }
    if (consume_account_resync(session, scratch, response, written, touchesScratch)) {
        session.accountResyncFailures = 0;
        return true;
    }
    // A failed resync remains armed and blocks unrelated deferred output until it can be retried,
    // but not forever: past the limit the arm is dropped and the rest of the output flows again.
    if (session.accountResyncArmed) {
        if (++session.accountResyncFailures >= kAccountResyncFailureLimit) {
            session.accountResyncArmed = false;
            session.accountResyncFailures = 0;
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync result=abandoned reason=failures");
        }
        return false;
    }
    if (session.queuez.family4Active
        && (session.nightfallPowerRevision != state::activity::nightfall::power_revision())) {
        // A failed power refresh retries before lower-priority account images. Activity
        // keepalives are independent and must not be starved by a temporarily unavailable image.
        return push::activity::consume_activity_keepalive(session, scratch, response, written, touchesScratch);
    }
    if (lost_sector_rewards::consume(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (consume_completion_reward(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (festival_pickups::consume(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (forest_chest_rewards::consume(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (forest_loot_pickups::consume(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (!session.family4RepushArmed || session.family4RepushRoot == 0
        || GetTickCount64() < session.family4RepushDueTick) {
        return consume_banner_repush(session, scratch, response, written, touchesScratch)
               || push::activity::newlight_quest::consume(session, scratch, response, written, touchesScratch)
               || push::activity::launchpad_inventory::consume(session, scratch, response, written, touchesScratch)
               || push::activity::homecoming_inventory::consume(session, scratch, response, written, touchesScratch)
               || push::activity::adieu_inventory::consume(session, scratch, response, written, touchesScratch)
               || push::activity::quest_progress::consume(session, scratch, response, written, touchesScratch)
               || push::activity::consume_activity_keepalive(
                   session, scratch, response, written, touchesScratch);
    }
    // One attempt is owed, and it is spent whether or not it lands.
    touchesScratch = true;

    middleware::queuez::Subscription subscription{};
    subscription.familyType = queuez::kAccountFamilyType;
    subscription.familyRootSoid = session.family4RepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState after{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     state::bap().sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     after,
                                     armsRepush,
                                     armsBannerRepush);
    if (framedSize == 0 || framedSize > response.size()) {
        // Neither failure clears on a retry. Holding the arm starves the keepalive, and the client
        // drops the activity session once the keepalive stops.
        session.family4RepushArmed = false;
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         framedSize == 0 ? "ev=queuez stage=repush result=fail reason=encode"
                                         : "ev=queuez stage=repush result=fail reason=capacity");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    if (queuez::valid(after)) {
        session.queuez = after;
    }
    session.family4RepushArmed = false;
    report_repush("repush", framedSize);
    return true;
}

} // namespace dawn::server::bap::encrypted
