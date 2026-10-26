#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <limits>

#include "../../core/logging/log.h"
#include "../../state/activity/runtime.h"
#include "../../state/matchmaking/matchmaking_state.h"
#include "../gameplay/group/group_host_sessions.h"
#include "internal.h"
#include "runtime.h"

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
#include "activity_retirement_test_support.h"
#endif

namespace dawn::server::bap {
namespace {

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Session, kSessionCount> g_sessions{};
Scratch g_scratch{};
/** Process-wide connection generations never reset or live inside reusable session slots. */
state::activity::ConnectionGeneration g_connectionClock{};
bool g_connectionGenerationExhausted{};
std::uint64_t g_accountGeneration{};
/** Explicit last accepted creator-owned authoritative region event for the HUD. */
HudRegionAnchor g_hudRegionAnchor{};
/** Exact roots whose fenced source cascade is waiting for a pinned group row. */
std::array<state::activity::ActivityInstanceKey, state::activity::kSessionCapacity>
    g_pendingActivityRetirements{};
std::size_t g_pendingActivityRetirementCount = 0;

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
std::atomic<test_support::Hook> g_testHook{};

void invoke_test_hook(test_support::Point point,
                      state::activity::ActivityInstanceKey activity = {},
                      state::activity::RetireResult result =
                          state::activity::RetireResult::alreadyRetired,
                      bool completed = true) noexcept {
    const test_support::Hook hook = g_testHook.load(std::memory_order_acquire);
    if (hook != nullptr) {
        hook(point, activity, result, completed);
    }
}
#endif

/** Retains one exact root until its entire fenced source cascade can finish. */
void queue_pending_activity_retirement_locked(
    state::activity::ActivityInstanceKey activity) noexcept {
    if (!static_cast<bool>(activity)) {
        return;
    }
    for (std::size_t index = 0; index < g_pendingActivityRetirementCount; ++index) {
        if (g_pendingActivityRetirements[index] == activity) {
            return;
        }
    }
    if (g_pendingActivityRetirementCount < g_pendingActivityRetirements.size()) {
        g_pendingActivityRetirements[g_pendingActivityRetirementCount] = activity;
        ++g_pendingActivityRetirementCount;
    }
}

/** Clears every borrowed publication of one exact record. The BAP lock is already held. */
void invalidate_borrowed_activity_locked(
    state::activity::ActivityInstanceKey activity) noexcept {
    if (!static_cast<bool>(activity)) {
        return;
    }
    for (Session& session : g_sessions) {
        if (session.activity.instance == activity
            && !lifecycle::owns_published_activity(session.activity)) {
            session.activity = ActivityBindingState{};
        }
    }
}

/** Defense in depth for a borrowed record retired outside the ordered group path. */
void detach_stale_borrowed_activity(Session& session) noexcept {
    const bool exactRecordLive = static_cast<bool>(session.activity.instance)
                                 && state::activity::contains(session.activity.instance);
    if (lifecycle::borrowed_binding_is_stale(session.activity, exactRecordLive)) {
        session.activity = ActivityBindingState{};
    }
}

/** Detaches one session's staged binding state, then retires its optional BAP lease. */
void retire_session_activity_locked(Session& session) noexcept {
    const state::activity::ActivityInstanceKey owned =
        lifecycle::detach_owned_activity(session.activity);
    if (static_cast<bool>(owned)) {
        retire_bap_activity_lease_locked(owned);
    }
}

/**
 * Arms every other active peer after one shared-account transaction is published. The origin
 * already carries the transaction in its own reply, so it is armed only when that reply could not.
 */
void publish_account_mutation(Session& origin) noexcept {
    origin.accountMutationPublished = false;
    g_accountGeneration = g_accountGeneration == (std::numeric_limits<std::uint64_t>::max)()
                              ? 1
                              : g_accountGeneration + 1;
    origin.accountGeneration = g_accountGeneration;
    origin.accountResyncGeneration = g_accountGeneration;
    origin.accountResyncArmed = origin.accountResyncSelf;
    origin.accountResyncFailures = 0;
    origin.accountResyncSelf = false;
    std::size_t armed = 0;
    for (auto& peer : g_sessions) {
        if (&peer == &origin || peer.id == 0 || !peer.authenticated || !peer.queuez.family4Active) {
            continue;
        }
        peer.accountResyncGeneration = g_accountGeneration;
        peer.accountResyncArmed = true;
        peer.accountResyncFailures = 0;
        ++armed;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=queuez stage=peer_resync_arm result=ok generation=%llu "
                                    "origin=%u peers=%zu",
                                    static_cast<unsigned long long>(g_accountGeneration),
                                    origin.id,
                                    armed);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/** @param id Nonzero connection id. @return Matching open session, or null. */
[[nodiscard]] Session* session_for(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size()) {
        return nullptr;
    }
    auto& session = g_sessions[id - 1];
    return session.id == id && static_cast<bool>(session.connectionKey)
                   && session.connectionKey.connectionId == id
                   && !session.matchmakingRetirementPending
               ? &session
               : nullptr;
}

/** @param session Its secrets and identity are wiped. */
void clear_session(Session& session) noexcept {
    SecureZeroMemory(&session, sizeof session);
    // Secure zeroing alone would lose semantic sentinels such as activityAdvertisedRegion == -1.
    session = Session{};
}

/**
 * Releases an authenticated session's optional matchmaking context.
 * @param session Open session that may not have finished server hello.
 * @return True when there was no context, or the active generation was released.
 */
[[nodiscard]] bool release_matchmaking_context(Session& session) noexcept {
    if (session.matchmakingContext.generation == state::matchmaking::kInvalidGeneration) {
        session.matchmakingContext = {};
        session.matchmakingRetirementPending = false;
        return true;
    }
    if (!state::matchmaking::release_context(session.matchmakingContext)) {
        return false;
    }
    session.matchmakingContext = {};
    session.matchmakingRetirementPending = false;
    return true;
}

/** Securely discards transport/auth state while retaining one failed context cleanup retry. */
void quarantine_deferred_matchmaking_retirement(Session& session) noexcept {
    const lifecycle::DeferredMatchmakingRetirement pending =
        lifecycle::deferred_matchmaking_retirement(session);
    SecureZeroMemory(&session, sizeof session);
    lifecycle::restore_deferred_matchmaking_retirement(session, pending);
}

void report_matchmaking_retirement_failure(const Session& session,
                                           const char* boundary) noexcept;

/** @param id Session-slot id. @return True when the slot is opened. */
[[nodiscard]] bool open_session(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size()) {
        return false;
    }
    auto& session = g_sessions[id - 1];
    if (session.id != 0) {
        retire_session_activity_locked(session);
        if (!release_matchmaking_context(session)) {
            report_matchmaking_retirement_failure(session, "open");
            quarantine_deferred_matchmaking_retirement(session);
            return false;
        }
    }
    clear_session(session);
    state::activity::ConnectionKey connectionKey{};
    if (!lifecycle::allocate_connection_key(
            id, g_connectionClock, g_connectionGenerationExhausted, connectionKey)) {
        return false;
    }
    session.id = id;
    session.connectionKey = connectionKey;
    return true;
}

/** Records a failed context cleanup without letting it hide an activity retirement. */
void report_matchmaking_retirement_failure(const Session& session,
                                           const char* boundary) noexcept {
    std::array<char, 192> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=bap stage=matchmaking_retire result=deferred boundary=%s connection=%u slot=%zu "
        "generation=%u",
        boundary,
        session.id,
        session.matchmakingContext.slot,
        session.matchmakingContext.generation);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** @param id Session-slot id. @return True when the slot is cleared. */
[[nodiscard]] bool close_session(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size()) {
        return false;
    }
    auto& session = g_sessions[id - 1];
    if (session.id != 0) {
        retire_session_activity_locked(session);
        if (!release_matchmaking_context(session)) {
            report_matchmaking_retirement_failure(session, "close");
            quarantine_deferred_matchmaking_retirement(session);
            return false;
        }
    }
    clear_session(session);
    return true;
}

/**
 * Routes one validated frame through its connection-owned session.
 * @param request Frame event and caller-owned buffers.
 * @param response Receives encoded response size.
 * @return True when the frame is valid and its service is handled.
 */
[[nodiscard]] bool consume_frame(const client::network::BapRequest& request,
                                 client::network::BapResponse& response) noexcept {
    middleware::bap::OuterFrame frame;
    if (!middleware::bap::parse_frame(request.frame, frame)) {
        return false;
    }
    auto* session = session_for(request.connectionId);
    if (session == nullptr) {
        return false;
    }
    detach_stale_borrowed_activity(*session);
    bool handled = false;
    if (frame.frameType == middleware::bap::FrameType::encrypted) {
        if (!session->authenticated || !lifecycle::authentication_key_is_current(*session)) {
            return false;
        }
        handled = encrypted::consume(*session, g_scratch, frame, request.response, response.size);
    } else {
        handled = plaintext::consume(*session, g_scratch, frame, request.response, response.size);
    }
    if (!handled) {
        return false;
    }
    if (frame.frameType == middleware::bap::FrameType::encrypted
        && session->accountMutationPublished) {
        publish_account_mutation(*session);
    }
    // A frame response can carry one already-due push in the same bounded socket write.
    bool touchesScratch = true;
    std::size_t deferred = 0;
    if (response.size < request.response.size()
        && encrypted::consume_deferred(*session,
                                       g_scratch,
                                       request.response.subspan(response.size),
                                       deferred,
                                       touchesScratch)) {
        response.size += deferred;
        if (session->accountMutationPublished) { publish_account_mutation(*session); }
    }
    if (session->accountMutationPublished) {
        publish_account_mutation(*session);
    }
    return true;
}

/**
 * Services one timed poll for a session that may owe a deferred push.
 * @param request Poll event and caller-owned output buffer.
 * @param response Receives encoded notification size.
 * @param touchesScratch Set when the attempt reaches a scratch buffer.
 * @return True when a notification is published.
 */
[[nodiscard]] bool consume_poll(const client::network::BapRequest& request,
                                client::network::BapResponse& response,
                                bool& touchesScratch) noexcept {
    static std::atomic_bool reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::info, "ev=queuez stage=poll result=ok");
    }
    auto* session = session_for(request.connectionId);
    if (session != nullptr) {
        detach_stale_borrowed_activity(*session);
    }
    const bool consumed = session != nullptr && session->authenticated
                          && lifecycle::authentication_key_is_current(*session)
                          && encrypted::consume_deferred(
                              *session, g_scratch, request.response, response.size, touchesScratch);
    if (consumed && session->accountMutationPublished) {
        publish_account_mutation(*session);
    }
    return consumed;
}

/** One bounded cascade attempt, which may defer while a lineage row remains pinned. */
struct ActivityTreeRetirement final {
    state::activity::RetireResult result{state::activity::RetireResult::alreadyRetired};
    bool completed{};
};

/** Recursively retires exact derived rows before their source. The BAP lock is already held. */
[[nodiscard]] ActivityTreeRetirement retire_activity_tree_locked(
    state::activity::ActivityInstanceKey source,
    std::array<state::activity::ActivityInstanceKey,
               lifecycle::kActivityRetirementCascadeCapacity>& visited,
    std::size_t& visitedCount) noexcept {
    if (!static_cast<bool>(source)) {
        return {state::activity::RetireResult::alreadyRetired, true};
    }
    for (std::size_t index = 0; index < visitedCount; ++index) {
        if (visited[index] == source) {
            return {state::activity::RetireResult::alreadyRetired, true};
        }
    }
    if (visitedCount >= visited.size()) {
        return {state::activity::RetireResult::alreadyRetired, false};
    }
    visited[visitedCount] = source;
    ++visitedCount;

    std::array<state::activity::ActivityInstanceKey,
               gameplay::group::kHostRecordRetirementCapacity>
        derived{};
    std::size_t derivedCount = 0;
    const bool sourceReady = gameplay::group::begin_host_session_source_retirement(
        source, derived, derivedCount);
#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
    invoke_test_hook(test_support::Point::sourceBeginComplete,
                     source,
                     state::activity::RetireResult::alreadyRetired,
                     sourceReady);
#endif
    if (!sourceReady) {
        return {state::activity::RetireResult::alreadyRetired, false};
    }
    const lifecycle::ActivityRetirementCascade cascade =
        lifecycle::prepare_retirement_cascade(source,
                                              std::span(derived).first(derivedCount));
    for (std::size_t index = 0; index < cascade.count; ++index) {
        if (cascade.keys[index] != source) {
            const ActivityTreeRetirement derivedRetirement =
                retire_activity_tree_locked(cascade.keys[index], visited, visitedCount);
            if (!derivedRetirement.completed) {
                return {state::activity::RetireResult::alreadyRetired, false};
            }
            gameplay::group::commit_host_session_derived_retirement(
                source, cascade.keys[index]);
        }
    }
    gameplay::group::commit_host_session_source_retirement(source);
    invalidate_borrowed_activity_locked(source);
    const state::activity::RetireResult result =
        state::activity::retire_session_exact(source);
#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
    invoke_test_hook(test_support::Point::sourceStateRetired, source, result, true);
#endif
    gameplay::group::finish_host_session_source_retirement(source);
    return {result, true};
}

/** Retries a stable snapshot; blocked roots remain in the fixed pending set. */
void drain_pending_activity_retirements_locked() noexcept {
    const auto pending = g_pendingActivityRetirements;
    const std::size_t pendingCount = g_pendingActivityRetirementCount;
    g_pendingActivityRetirements = {};
    g_pendingActivityRetirementCount = 0;
    for (std::size_t index = 0; index < pendingCount; ++index) {
        std::array<state::activity::ActivityInstanceKey,
                   lifecycle::kActivityRetirementCascadeCapacity>
            visited{};
        std::size_t visitedCount = 0;
        const ActivityTreeRetirement attempt =
            retire_activity_tree_locked(pending[index], visited, visitedCount);
        if (!attempt.completed) {
            queue_pending_activity_retirement_locked(pending[index]);
        }
    }
}

} // namespace

/** True only while an authenticated current BAP binding owns the exact creator root. */
bool has_current_bap_creator_locked(
    state::activity::ActivityInstanceKey source) noexcept {
    if (!static_cast<bool>(source)) {
        return false;
    }
    bool owned = false;
    for (const Session& candidate : g_sessions) {
        if (lifecycle::activity_binding_is_current(candidate)
            && lifecycle::owns_published_activity(candidate.activity)
            && candidate.activity.ownerLease.activity == source
            && candidate.activity.instance == source) {
            owned = true;
            break;
        }
    }
    return owned && state::activity::contains(source);
}

/** Resolves the exact target -> creator-root proof for a successor binding. */
bool prepare_region_lineage_locked(
    const Session& session,
    state::activity::ActivityInstanceKey next,
    bool fromJoin,
    RegionLineage& output,
    gameplay::group::HostActivityLineageLease& groupLease) noexcept {
    output = {};
    gameplay::group::release_host_activity_lineage(groupLease);
    if (!static_cast<bool>(next)) {
        return false;
    }
    if (!fromJoin) {
        output = {next, next, RegionLineageKind::ownedActivity};
        return true;
    }
    if (session.activity.instance == next && static_cast<bool>(session.activity.lineage)) {
        output = session.activity.lineage;
        if (!acquire_region_lineage_locked(session, output, groupLease)) {
            gameplay::group::release_host_activity_lineage(groupLease);
            output = {};
            return false;
        }
        return true;
    }
    if (has_current_bap_creator_locked(next)) {
        output = {next, next, RegionLineageKind::ownedActivity};
        return true;
    }
    if (!gameplay::group::acquire_host_activity_lineage(next, groupLease)
        || groupLease.host != next || !static_cast<bool>(groupLease.source)
        || (lifecycle::owns_published_activity(session.activity)
            && session.activity.ownerLease.activity == groupLease.source)
        || !has_current_bap_creator_locked(groupLease.source)) {
        gameplay::group::release_host_activity_lineage(groupLease);
        return false;
    }
    output = {next, groupLease.source, RegionLineageKind::groupDerivedBorrow};
    return true;
}

/** Revalidates a current binding's exact creator owner and group edge. */
bool region_lineage_is_current_locked(const Session& session,
                                      const RegionLineage& lineage) noexcept {
    gameplay::group::HostActivityLineageLease lease{};
    const bool current = acquire_region_lineage_locked(session, lineage, lease);
    gameplay::group::release_host_activity_lineage(lease);
    return current;
}

/** Proves one exact lineage and retains its borrowed group-row pin for the caller. */
bool acquire_region_lineage_locked(
    const Session& session,
    const RegionLineage& lineage,
    gameplay::group::HostActivityLineageLease& lease) noexcept {
    gameplay::group::release_host_activity_lineage(lease);
    if (lineage.kind == RegionLineageKind::ownedActivity) {
        return retained_region_lineage_is_current_locked(session, lineage, lease);
    }
    if (lineage.kind != RegionLineageKind::groupDerivedBorrow
        || lineage.bound == lineage.source
        || !has_current_bap_creator_locked(lineage.source)
        || !gameplay::group::acquire_host_activity_lineage(lineage.bound, lease)
        || lease.host != lineage.bound || lease.source != lineage.source
        || !gameplay::group::validate_host_activity_lineage(lease)) {
        gameplay::group::release_host_activity_lineage(lease);
        return false;
    }
    return retained_region_lineage_is_current_locked(session, lineage, lease);
}

/** Revalidates the exact creator and the caller's still-pinned borrowed bound edge. */
bool retained_region_lineage_is_current_locked(
    const Session& session,
    const RegionLineage& lineage,
    const gameplay::group::HostActivityLineageLease& lease) noexcept {
    if (!lifecycle::activity_binding_is_current(session) || !static_cast<bool>(lineage)
        || lineage.bound != session.activity.instance) {
        return false;
    }
    if (lineage.kind == RegionLineageKind::ownedActivity) {
        return !lease.pinned && lineage.bound == lineage.source
               && lifecycle::owns_published_activity(session.activity)
               && session.activity.ownerLease.activity == lineage.source
               && state::activity::contains(lineage.source);
    }
    return lineage.kind == RegionLineageKind::groupDerivedBorrow
           && lineage.bound != lineage.source && lease.pinned
           && lease.host == lineage.bound && lease.source == lineage.source
           && has_current_bap_creator_locked(lineage.source)
           && gameplay::group::validate_host_activity_lineage(lease);
}

/** Publishes an exact HUD anchor only for the reporting creator binding. */
void publish_hud_region_locked(Session& session,
                               state::activity::HostRegionKey hostRegion) noexcept {
    if (!lifecycle::activity_binding_is_current(session)
        || !lifecycle::owns_published_activity(session.activity)
        || session.activity.lineage.kind != RegionLineageKind::ownedActivity
        || session.activity.lineage.bound != session.activity.instance
        || session.activity.lineage.source != session.activity.instance
        || hostRegion.activity != session.activity.instance) {
        return;
    }
    g_hudRegionAnchor = {session.activity.key, session.activity.instance, hostRegion};
}

/** Carries an anchor through only a same-exact owner transfer. */
void update_hud_anchor_after_binding_replacement_locked(
    const ActivityBindingState& previous,
    const ActivityBindingState& next) noexcept {
    if (g_hudRegionAnchor.ownerBinding != previous.key) {
        return;
    }
    if (next.instance == previous.instance && lifecycle::owns_published_activity(next)
        && next.lineage.kind == RegionLineageKind::ownedActivity
        && next.lineage.source == next.instance) {
        g_hudRegionAnchor.ownerBinding = next.key;
        return;
    }
    g_hudRegionAnchor = {};
}

/** Copies one coherent owner-validated HUD destination/region view. */
bool snapshot_hud_region(HudRegionSnapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const HudRegionAnchor anchor = g_hudRegionAnchor;
    const Session* owner = nullptr;
    for (const Session& candidate : g_sessions) {
        if (candidate.activity.key == anchor.ownerBinding) {
            owner = &candidate;
            break;
        }
    }
    state::activity::membership::RegionView view{};
    const bool valid = owner != nullptr && static_cast<bool>(anchor.ownerBinding)
                       && lifecycle::activity_binding_is_current(*owner)
                       && lifecycle::owns_published_activity(owner->activity)
                       && owner->activity.instance == anchor.activity
                       && owner->activity.ownerLease.activity == anchor.activity
                       && state::activity::membership::snapshot_region_view(
                           anchor.activity, anchor.hostRegion, view);
    ReleaseSRWLockShared(&g_lock);
    if (!valid) {
        return false;
    }
    output.lineage = anchor;
    output.destination = view.destination;
    output.reportedRegion = view.reportedRegion;
    return true;
}

/** Securely retires the current authentication while retaining its connection-owned allocator. */
void reset_auth_owned_state(Session& session) noexcept {
    retire_session_activity_locked(session);
    const lifecycle::ConnectionOwnedState connection = lifecycle::connection_owned_state(session);
    SecureZeroMemory(&session, sizeof session);
    lifecycle::restore_connection_owned_state(session, connection);
}

/** Retires one BAP lease and the exact group-owned rows derived from it. */
void retire_bap_activity_lease_locked(
    state::activity::ActivityInstanceKey activity) noexcept {
    std::array<state::activity::ActivityInstanceKey,
               lifecycle::kActivityRetirementCascadeCapacity>
        visited{};
    std::size_t visitedCount = 0;
    const ActivityTreeRetirement retirement =
        retire_activity_tree_locked(activity, visited, visitedCount);
    if (!retirement.completed) {
        queue_pending_activity_retirement_locked(activity);
    }
}

/** Invalidates BAP borrowers and retires one group-owned activity under the same lock. */
state::activity::RetireResult retire_group_owned_activity(
    state::activity::ActivityInstanceKey activity) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    std::array<state::activity::ActivityInstanceKey,
               lifecycle::kActivityRetirementCascadeCapacity>
        visited{};
    std::size_t visitedCount = 0;
    const ActivityTreeRetirement retirement =
        retire_activity_tree_locked(activity, visited, visitedCount);
    if (!retirement.completed) {
        queue_pending_activity_retirement_locked(activity);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return retirement.result;
}

/**
 * Arms every authenticated account peer after the loadout editor commits out of band.
 * A BAP request publishes its own mutation and skips its origin, because that peer already
 * carries the change. An editor apply has no origin session, so every peer is armed, the local
 * Client included, and each one rebuilds from committed State on its next service poll.
 */
std::size_t publish_external_account_mutation() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_accountGeneration = g_accountGeneration == (std::numeric_limits<std::uint64_t>::max)()
                              ? 1
                              : g_accountGeneration + 1;
    std::size_t armed = 0;
    for (auto& peer : g_sessions) {
        if (peer.id == 0 || !peer.authenticated || !peer.queuez.family4Active) {
            continue;
        }
        peer.accountResyncGeneration = g_accountGeneration;
        peer.accountResyncArmed = true;
        peer.accountResyncFailures = 0;
        ++armed;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=queuez stage=editor_resync_arm result=ok generation=%llu "
                                    "peers=%zu",
                                    static_cast<unsigned long long>(g_accountGeneration),
                                    armed);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    ReleaseSRWLockExclusive(&g_lock);
    return armed;
}

/** Nonblocking retry after a group lineage pin drops. */
void retry_pending_activity_retirements() noexcept {
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        return;
    }
    drain_pending_activity_retirements_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

/** Applies one serialized BAP connection lifecycle event. */
bool consume(const client::network::BapRequest& request,
             client::network::BapResponse& response) noexcept {
    response = {};
    AcquireSRWLockExclusive(&g_lock);
#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
    invoke_test_hook(test_support::Point::eventLockAcquired);
#endif
    bool success = false;
    // Polls report whether they reached scratch.
    bool touchesScratch = request.event != client::network::BapEvent::poll;
    // Hold the session lock across cryptographic counter reads and updates.
    switch (request.event) {
    case client::network::BapEvent::open:
        success = open_session(request.connectionId);
        break;
    case client::network::BapEvent::frame:
        success = consume_frame(request, response);
        break;
    case client::network::BapEvent::poll:
        success = consume_poll(request, response, touchesScratch);
        break;
    case client::network::BapEvent::close:
        success = close_session(request.connectionId);
        break;
    }
    drain_pending_activity_retirements_locked();
    // Decrypted frames can contain runtime-only keys or tokens, so scratch never outlives the call.
    if (touchesScratch) {
        SecureZeroMemory(&g_scratch, sizeof g_scratch);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

/** Securely erases every connection-owned nonce and transform buffer. */
void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    drain_pending_activity_retirements_locked();
    for (auto& session : g_sessions) {
        // Activity cleanup cannot be conditional on the independent matchmaking release.
        retire_session_activity_locked(session);
        if (session.id != 0
            && session.matchmakingContext.generation != state::matchmaking::kInvalidGeneration) {
            // State erases runtime descriptors before the opaque association is cleared.
            if (!release_matchmaking_context(session)) {
                report_matchmaking_retirement_failure(session, "shutdown");
            }
        }
        clear_session(session);
    }
    drain_pending_activity_retirements_locked();
    SecureZeroMemory(&g_scratch, sizeof g_scratch);
    g_accountGeneration = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

#if defined(DAWN_ACTIVITY_RETIREMENT_TESTS)
namespace test_support {

void set_hook(Hook hook) noexcept {
    g_testHook.store(hook, std::memory_order_release);
}

Snapshot snapshot() noexcept {
    Snapshot copied{};
    AcquireSRWLockShared(&g_lock);
    copied.pending = g_pendingActivityRetirements;
    copied.pendingCount = g_pendingActivityRetirementCount;
    for (const Session& session : g_sessions) {
        if (lifecycle::owns_published_activity(session.activity)) {
            ++copied.ownerLeaseCount;
        } else if (static_cast<bool>(session.activity.instance)) {
            ++copied.borrowedBindingCount;
        }
        if (session.matchmakingRetirementPending) {
            ++copied.quarantinedCount;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return copied;
}

bool snapshot_session(std::uint32_t id, Session& output) noexcept {
    output = {};
    if (id == 0 || id > g_sessions.size()) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    output = g_sessions[id - 1];
    ReleaseSRWLockShared(&g_lock);
    return output.id != 0;
}

bool configure_session(std::uint32_t id,
                       state::activity::ActivityInstanceKey activity,
                       bool ownsActivity,
                       state::matchmaking::ContextHandle context) noexcept {
    if (id == 0 || id > g_sessions.size()
        || (ownsActivity && !static_cast<bool>(activity))) {
        return false;
    }
    bool configured = false;
    AcquireSRWLockExclusive(&g_lock);
    Session& session = g_sessions[id - 1];
    if (session.id == id && static_cast<bool>(session.connectionKey)
        && !session.matchmakingRetirementPending) {
        const lifecycle::ConnectionOwnedState connection =
            lifecycle::connection_owned_state(session);
        SecureZeroMemory(&session, sizeof session);
        lifecycle::restore_connection_owned_state(session, connection);
        session.authenticationClock =
            state::activity::AuthenticationGeneration{state::activity::kFirstGeneration};
        session.authenticationKey = {session.connectionKey, session.authenticationClock};
        session.authenticated = true;
        session.matchmakingContext = context;
        if (static_cast<bool>(activity)) {
            session.activityBindingClock =
                state::activity::BindingGeneration{state::activity::kFirstGeneration};
            session.activity.key = {session.authenticationKey, session.activityBindingClock};
            session.activity.instance = activity;
            if (ownsActivity) {
                session.activity.ownerLease = {
                    activity, ActivityRecordCreator::bapService6};
                session.activity.lineage = {
                    activity, activity, RegionLineageKind::ownedActivity};
            }
        }
        configured = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return configured;
}

bool with_session_locked(std::uint32_t id, SessionAction action) noexcept {
    if (action == nullptr) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Session* session = session_for(id);
    const bool result = session != nullptr && action(*session);
    drain_pending_activity_retirements_locked();
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

bool reset_authentication(std::uint32_t id) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    Session* session = session_for(id);
    if (session != nullptr) {
        reset_auth_owned_state(*session);
    }
    drain_pending_activity_retirements_locked();
    ReleaseSRWLockExclusive(&g_lock);
    return session != nullptr;
}

void reset_storage() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_sessions = {};
    SecureZeroMemory(&g_scratch, sizeof g_scratch);
    g_connectionClock = {};
    g_connectionGenerationExhausted = false;
    g_accountGeneration = 0;
    g_hudRegionAnchor = {};
    g_pendingActivityRetirements = {};
    g_pendingActivityRetirementCount = 0;
    ReleaseSRWLockExclusive(&g_lock);
    set_hook(nullptr);
}

} // namespace test_support
#endif

} // namespace dawn::server::bap
