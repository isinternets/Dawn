#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "../../client/network/consumer.h"
#include "../../middleware/bap/activity_message/activity_patch_epoch_parser.h"
#include "../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../middleware/bap/frame.h"
#include "../../state/activity/bubble_authority/definition.h"
#include "../../state/activity/lifecycle_generation.h"
#include "../../state/activity/omega_ikora_lattice.h"
#include "../../state/activity/coo/omega_opening.h"
#include "../../state/build_data/scenarios/definition.h"
#include "../../state/runtime/state.h"
#include "encrypted/queuez/definition.h"
#include "region_lineage.h"

namespace dawn::server::bap {

/** One session per transport peer slot, so a connection id indexes this array directly. */
inline constexpr std::size_t kSessionCount = client::network::kBapConnectionCount;

/** Fixed scratch storage owned by the lock, kept off the Client thread's stack. */
struct Scratch {
    std::array<std::byte, client::network::kBapFrameCapacity> plaintext{};
    std::array<std::byte, client::network::kBapFrameCapacity> responseBody{};
    std::array<std::byte, client::network::kBapFrameCapacity> responsePayload{};
    std::array<std::byte, client::network::kBapFrameCapacity> sealed{};
    std::array<std::byte, client::network::kBapFrameCapacity> framed{};
    /** Roster groups the outbound body's slot spans point into. */
    std::array<state::build_data::scenarios::RosterGroup,
               middleware::bap::activity_message::sensor_auth_update::kGroupCapacity>
        rosterGroups{};
    /** Bubble-local roster views and key storage referenced by outbound spans. */
    std::array<middleware::bap::activity_message::sensor_auth_update::BubbleSubBlock, 64>
        rosterSubBlocks{};
    std::array<std::array<std::uint32_t,
                          middleware::bap::activity_message::sensor_auth_update::kGroupCapacity>,
               64>
        rosterSubBlockKeys{};
};

/**
 * What one staged roster body owes State, and the counters to put back if it is discarded.
 * A bubble is offered once, and the state byte rebuilds every object the roster owns. Both may
 * move only once the frame reaches the caller.
 */
struct RosterPublication {
    /** Exact transport binding that staged this body. */
    state::activity::BindingKey binding{};
    /** Exact host activity whose snapshot the body carries. */
    state::activity::ActivityInstanceKey activity{};
    /** Non-wrapping publication identity within the owning binding. */
    state::activity::PublicationGeneration publication{};
    /** Exact creator-root region generation this body was built from. */
    state::activity::HostRegionKey sourceHostRegion{};
    std::int32_t regionIndex{-1};
    state::activity::bubble_authority::Grant grant{};
    /** Correlates the staged JSONL auth record with its delivered or discarded outcome. */
    std::uint64_t omegaTracePublicationId{};
    std::uint64_t priorVendorPresence{},priorVendorClockOrigin{};
    std::uint64_t afterVendorPresence{},afterVendorClockOrigin{};
    std::uint32_t priorGroups{};
    std::uint8_t priorSends{};
    std::uint8_t priorState{};
    std::uint8_t priorOmegaOpeningStage{};
    std::uint16_t priorDirectorSends{};
    bool priorMissionDirectorActive{};
    encrypted::push::activity::roster_lifetime::State priorLifetimes{};
    std::uint32_t afterGroups{};
    std::uint8_t afterSends{};
    std::uint8_t afterState{};
    std::uint8_t afterOmegaOpeningStage{};
    std::uint16_t afterDirectorSends{};
    bool afterMissionDirectorActive{};
    encrypted::push::activity::roster_lifetime::State afterLifetimes{};
    /** Opening stage carried by this body; zero is the baseline full-seed packet. */
    std::uint8_t omegaOpeningStage{};
    /** Omega script state carried by this staged body: zero when it carries no runtime edge. */
    std::uint8_t omegaOpeningScriptState{};
    /** Tower Watch manifest stage carried by this body; committed only after delivery. */
    std::uint8_t towerWatchCueStage{};
    /** Set when the staged body carried a bubble grant that State has not recorded yet. */
    bool hasGrant{};
    /** True when construction used a detached immutable delivery candidate. */
    bool hasAfter{};
    /** Set while a roster body is staged and its outcome is undecided. */
    bool staged{};
};

/** Connection-local validation, dedupe and delivery state for Omega's bounded opening edge. */
struct ActivitySensorObservation {
    state::activity::coo::omega::opening::Run omegaOpeningExecutor{};
    state::activity::omega_ikora_lattice::State omegaIkoraLattice{};
    /** Latest top-level roster mirror, used to identify a mission's root cue group dynamically. */
    std::array<std::uint32_t, 8> authoredTopLevelKeys{};
    std::array<std::uint64_t, 3> authoredRootCueBodyHashes{};
    std::array<std::uint32_t, 3> authoredRootCueBodyBits{};
    std::array<std::uint16_t, 3> authoredRootCueSlotIndexes{};
    std::array<bool, 3> authoredRootCueSeen{};
    std::uint32_t authoredRootRegistryKey{};
    std::uint8_t authoredTopLevelKeyCount{};
    /** Tower Watch cue interpreter input and delivery state. */
    std::uint64_t towerWatchEncounterBaselineHash{};
    std::uint64_t towerWatchEncounterBodyHash{};
    /** GetTickCount64 timestamp committed with the delivered opening cue. */
    std::uint64_t towerWatchOpeningCommittedTick{};
    /** Observe-only state for the candidate physical wall-approach monitor. */
    std::uint64_t towerWatchApproachBaselineHash{};
    std::uint64_t towerWatchApproachBodyHash{};
    std::uint8_t towerWatchPublishedStage{};
    bool towerWatchRosterReady{};
    bool towerWatchBreachSeen{};
    bool towerWatchApproachSeen{};
    bool towerWatchEncounterSeen{};
    bool towerWatchEncounterActive{};
    bool towerWatchEncounterChanged{};
    /** One exact recovered Omega bootstrap variant has been observed for mission_scot. */
    bool omegaRosterReady{};
    /** The exact D00142CF/type-30/index-20 entered body has been latched once. */
    bool omegaOpeningTriggered{};
    /** True only after the authoritative opening-ready state-1 body reached the caller. */
    bool omegaOpeningReadyAuthorityPublished{};
    /** True only after the authoritative script-state 2 body reached the caller. */
    bool omegaOpeningAuthorityPublished{};
    /** The authored type-43/index-1 140-bit phase arrived after the real opening trigger. */
    bool omegaSceneHandoffArmed{};
    /** A later native authored-cast retirement was consumed outside Destiny's teardown call. */
    bool omegaSceneCompleted{};
    /** The exact D00142CF/type-30/index-24 forest-entrance edge has been latched once. */
    bool omegaForestEntranceTriggered{};
    /** A committed type-22 teleport field or actual region move confirmed portal authority. */
    bool omegaPortalTransportConfirmed{};
    /** True only after the authoritative script-state 4 body reached the caller. */
    bool omegaForestEntranceAuthorityPublished{};
    /** The forest transition request has been handed to the client's game thread. */
    bool omegaForestTransitionRequested{};
    /** Tick of the first keepalive after the entrance latch, for the request fallback. */
    std::uint64_t omegaForestEntranceLatchTick{};
};

/** Typed creator of a State activity record leased to one BAP connection. */
enum class ActivityRecordCreator : std::uint8_t {
    none,
    /** Record created by this connection's successful service-6 transaction. */
    bapService6,
};

/** Exact State record this binding owns, distinct from its joined/borrowed role. */
struct ActivityRecordLease final {
    state::activity::ActivityInstanceKey activity{};
    ActivityRecordCreator creator{ActivityRecordCreator::none};

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return creator == ActivityRecordCreator::bapService6
               && static_cast<bool>(activity);
    }

    friend constexpr bool operator==(ActivityRecordLease, ActivityRecordLease) noexcept = default;
};

/** Replaceable delivery and observation state for one exact authenticated activity binding. */
struct ActivityBindingState {
    state::activity::BindingKey key{};
    state::activity::ActivityInstanceKey instance{};
    /** Present only when this connection must retire the bound State record. */
    ActivityRecordLease ownerLease{};
    /** Exact binding-owned target -> creator-root region proof. */
    RegionLineage lineage{};
    /** Whole-bundle retry after-image; absent for ordinary delivery. */
    RegionPublicationDebt regionDebt{};
    std::uint64_t keepaliveDueTick{};
    std::uint64_t memberKey{};
    std::uint64_t characterSoid{};
    std::uint64_t rosterDueTick{};
    std::uint64_t transitionUntilTick{};
    std::uint32_t rosterGroups{};
    std::uint8_t rosterSends{};
    std::uint8_t rosterState{};
    std::uint64_t towerVendorPresence{},vendorClockOrigin{};
    std::uint64_t vendorPopulationRevision{};
    encrypted::push::activity::roster_lifetime::State rosterLifetimes{};
    /** Temporary delivery-local home; the host-durable Omega move remains a later slice. */
    std::uint8_t omegaOpeningStage{};
    std::uint16_t directorSends{};
    bool missionDirectorActive{};
    ActivitySensorObservation sensorObservation{};
    bool joinedForeignSession{};
    std::int32_t advertisedRegion{-1};
    std::uint8_t rosterReason{};
    state::activity::PublicationGeneration rosterPublicationClock{};
    bool rosterPublicationGenerationExhausted{};
    RosterPublication rosterStaged{};
};

/** Mutable transport state owned by one BAP connection. */
struct Session {
    std::uint32_t id{};
    /** Exact process-unique lifetime currently occupying this transport slot. */
    state::activity::ConnectionKey connectionKey{};
    /** Exact authentication lifetime currently published on this connection. */
    state::activity::AuthenticationKey authenticationKey{};
    /** Connection-local allocator clock retained across successful reauthentication. */
    state::activity::AuthenticationGeneration authenticationClock{};
    /** Sticky fail-closed state after the authentication clock cannot advance. */
    bool authenticationGenerationExhausted{};
    bool authenticated{};
    std::array<std::byte, state::kBapNonceSize> sendNonce{};
    std::array<std::byte, state::kBapNonceSize> receiveNonce{};
    /** Opaque State handle taken only after the server hello authenticates. */
    state::matchmaking::ContextHandle matchmakingContext{};
    /** Transport is closed and only a failed exact context retirement remains retryable. */
    bool matchmakingRetirementPending{};
    /** Authentication-owned allocator for child activity bindings. */
    state::activity::BindingGeneration activityBindingClock{};
    bool activityBindingGenerationExhausted{};
    /** The client's own patch epoch, from message 52. The roster body splices it verbatim. */
    middleware::bap::activity_message::patch_epoch::PatchEpoch activityPatchEpoch{};
    /** Set once message 52 has arrived, which is what makes a roster update sendable. */
    bool activityPatchEpochSeen{};
    /** Monotonic ordinal assigned to type-6 envelopes on this authenticated link. */
    std::uint64_t activitySensorPacketSequence{};
    /** Replaceable child binding; patch epoch and packet ordinal intentionally remain outside. */
    ActivityBindingState activity{};
    /** Queuez versions and residents published only through this authenticated peer. */
    encrypted::queuez::SessionState queuez{};
    /** Tick count after which the owed Family-4 re-push may go out. */
    std::uint64_t family4RepushDueTick{};
    std::uint64_t questProgressDueTick{};
    /** Root the owed re-push must use. */
    std::uint64_t family4RepushRoot{};
    /** True while one Family-4 re-push is still owed to this peer. */
    bool family4RepushArmed{};
    /** Tick count after which the owed banner re-push may go out. */
    std::uint64_t bannerRepushDueTick{};
    /** Root the owed banner re-push must use. */
    std::uint64_t bannerRepushRoot{};
    /** True while one banner re-push is still owed to this peer. */
    bool bannerRepushArmed{};
    /** Latest shared-account generation this peer has received. */
    std::uint64_t accountGeneration{};
    /** Newest shared-account generation owed as a full cross-peer refresh. */
    std::uint64_t accountResyncGeneration{};
    /** Latest native Nightfall power projection revision published to this peer. */
    std::uint64_t nightfallPowerRevision{};
    /** Set by encrypted processing only after one account mutation commits and is copied out. */
    bool accountMutationPublished{};
    /** True while another peer's account mutation still needs a full local refresh. */
    bool accountResyncArmed{};
    /**
     * Set by encrypted processing when the request that mutated the account left this peer's own
     * Family-4 graph short of it, so the peer's own refresh is armed along with the others.
     */
    bool accountResyncSelf{};
    /** Consecutive failed attempts at the armed refresh; the arm is dropped past a bound. */
    std::uint8_t accountResyncFailures{};
};

/**
 * Failed refresh attempts after which the arm is dropped rather than retried every tick.
 * A refresh that cannot be built blocks every other deferred push for as long as it stays armed,
 * and the Client, starved of all of them, gives up its session. Dropping the arm leaves the
 * Client on its last image until the next sign-in, which it survives.
 */
inline constexpr std::uint8_t kAccountResyncFailureLimit = 16;

// clear_session and authentication retirement securely wipe the whole object.
// Assignment restores data/sentinels, not hidden vtable or ownership machinery.
static_assert(std::is_trivially_copyable_v<Session>,
    "Session must remain trivially copyable for secure wipe and detached snapshots.");

namespace lifecycle {

/** Origin of a newly published activity binding. */
enum class ActivityBindingOrigin : std::uint8_t {
    allocation,
    join,
};

/** Maximum eight rows plus eight pending evictions and the exact source record. */
inline constexpr std::size_t kActivityRetirementCascadeCapacity = 17;

/** Ordered exact keys for one source retirement: every unique derived row, then its source. */
struct ActivityRetirementCascade final {
    std::array<state::activity::ActivityInstanceKey, kActivityRetirementCascadeCapacity> keys{};
    std::size_t count{};
};

/** Builds a duplicate-free derived-before-source retirement order without changing ownership. */
[[nodiscard]] inline ActivityRetirementCascade prepare_retirement_cascade(
    state::activity::ActivityInstanceKey source,
    std::span<const state::activity::ActivityInstanceKey> derived) noexcept {
    ActivityRetirementCascade result{};
    for (const state::activity::ActivityInstanceKey key : derived) {
        if (!static_cast<bool>(key) || key == source) {
            continue;
        }
        bool duplicate = false;
        for (std::size_t index = 0; index < result.count; ++index) {
            duplicate = duplicate || result.keys[index] == key;
        }
        if (!duplicate && result.count + 1U < result.keys.size()) {
            result.keys[result.count] = key;
            ++result.count;
        }
    }
    if (static_cast<bool>(source) && result.count < result.keys.size()) {
        result.keys[result.count] = source;
        ++result.count;
    }
    return result;
}

/** The fields whose lifetime is the connection rather than one authentication on it. */
struct ConnectionOwnedState final {
    std::uint32_t id{};
    state::activity::ConnectionKey connectionKey{};
    state::activity::AuthenticationGeneration authenticationClock{};
    bool authenticationGenerationExhausted{};
};

/** Non-secret evidence retained after a close whose matchmaking release failed. */
struct DeferredMatchmakingRetirement final {
    ConnectionOwnedState connection{};
    state::matchmaking::ContextHandle context{};
};

/** @return Only the fields that survive retirement of the current authentication. */
[[nodiscard]] inline ConnectionOwnedState connection_owned_state(
    const Session& session) noexcept {
    return {
        session.id,
        session.connectionKey,
        session.authenticationClock,
        session.authenticationGenerationExhausted,
    };
}

/**
 * Restores a value-initialized session carrying only its connection-owned fields.
 * The secure erase itself remains in reset_auth_owned_state so production cannot bypass it.
 */
inline void restore_connection_owned_state(Session& session,
                                           ConnectionOwnedState connection) noexcept {
    session = Session{};
    session.id = connection.id;
    session.connectionKey = connection.connectionKey;
    session.authenticationClock = connection.authenticationClock;
    session.authenticationGenerationExhausted =
        connection.authenticationGenerationExhausted;
}

/** Captures the only values retained when failed context cleanup quarantines a transport. */
[[nodiscard]] inline DeferredMatchmakingRetirement deferred_matchmaking_retirement(
    const Session& session) noexcept {
    return {connection_owned_state(session), session.matchmakingContext};
}

/** Restores a non-routable, secret-free context retirement retry. */
inline void restore_deferred_matchmaking_retirement(
    Session& session,
    DeferredMatchmakingRetirement pending) noexcept {
    restore_connection_owned_state(session, pending.connection);
    session.matchmakingContext = pending.context;
    session.matchmakingRetirementPending = true;
}

/** @return True only when the published authentication exactly belongs to this connection. */
[[nodiscard]] inline bool authentication_key_is_current(const Session& session) noexcept {
    return static_cast<bool>(session.connectionKey)
           && static_cast<bool>(session.authenticationKey)
           && session.authenticationKey.connection == session.connectionKey
           && session.authenticationKey.generation == session.authenticationClock;
}

/**
 * Allocates one exact connection key from a process-owned clock without wrapping it.
 * @param connectionId Protocol connection id to bind into the fresh key.
 * @param clock Process-wide connection generation clock.
 * @param exhausted Sticky process-wide exhaustion state.
 * @param output Cleared first, then receives the allocated key.
 * @return True only when a fresh, complete key was allocated.
 */
[[nodiscard]] inline bool allocate_connection_key(
    std::uint32_t connectionId,
    state::activity::ConnectionGeneration& clock,
    bool& exhausted,
    state::activity::ConnectionKey& output) noexcept {
    output = {};
    if (!state::activity::advance(clock, exhausted)) {
        return false;
    }
    output = {connectionId, clock};
    return static_cast<bool>(output);
}

/**
 * Allocates the next authentication key only after its complete service-26 response exists.
 * The returned key is staged rather than published so reset_auth_owned_state can retire the old
 * authentication before the caller arms the new one.
 * @param session Connection that owns the authentication allocator.
 * @param service26Encoded True only after the whole response is in caller-owned storage.
 * @param output Cleared first, then receives the staged key.
 * @return True only when an encoded response has a fresh, complete authentication key.
 */
[[nodiscard]] inline bool allocate_authentication_key(
    Session& session,
    bool service26Encoded,
    state::activity::AuthenticationKey& output) noexcept {
    output = {};
    if (!service26Encoded || !static_cast<bool>(session.connectionKey)
        || !state::activity::advance(session.authenticationClock,
                                     session.authenticationGenerationExhausted)) {
        return false;
    }
    output = {session.connectionKey, session.authenticationClock};
    return static_cast<bool>(output);
}

/** Stages the next binding key without publishing its authentication-owned clock. */
[[nodiscard]] inline bool stage_activity_binding_key(
    Session& session,
    state::activity::BindingKey& output) noexcept {
    output = {};
    if (!authentication_key_is_current(session)
        || session.activityBindingGenerationExhausted) {
        return false;
    }
    state::activity::BindingGeneration candidate = session.activityBindingClock;
    bool exhausted = false;
    if (!state::activity::advance(candidate, exhausted)) {
        session.activityBindingGenerationExhausted = exhausted;
        return false;
    }
    output = {session.authenticationKey, candidate};
    return static_cast<bool>(output);
}

/** @return True only for the unpublished next key under the current authentication. */
[[nodiscard]] inline bool is_staged_activity_binding_key(
    const Session& session,
    state::activity::BindingKey key) noexcept {
    if (!static_cast<bool>(key) || !authentication_key_is_current(session)
        || session.activityBindingGenerationExhausted
        || key.authentication != session.authenticationKey
        || session.activityBindingClock.value == state::activity::kMaximumGeneration) {
        return false;
    }
    return key.generation.value == session.activityBindingClock.value + 1U;
}

/** @return True only while the published child binding belongs to the current authentication. */
[[nodiscard]] inline bool activity_binding_is_current(const Session& session) noexcept {
    return authentication_key_is_current(session) && static_cast<bool>(session.activity.key)
           && static_cast<bool>(session.activity.instance)
           && session.activity.key.authentication == session.authenticationKey
           && session.activity.key.generation == session.activityBindingClock;
}

/** @return True only when the binding owns its exact published activity lifetime. */
[[nodiscard]] inline bool owns_published_activity(const ActivityBindingState& binding) noexcept {
    return static_cast<bool>(binding.ownerLease)
           && binding.ownerLease.activity == binding.instance;
}

/** @return True when a borrowed binding no longer names an exact live State record. */
[[nodiscard]] inline bool borrowed_binding_is_stale(
    const ActivityBindingState& binding,
    bool exactRecordLive) noexcept {
    return static_cast<bool>(binding.instance) && !owns_published_activity(binding)
           && !exactRecordLive;
}

/** Moves one binding's exact BAP lease out and invalidates all binding-staged work. */
[[nodiscard]] inline state::activity::ActivityInstanceKey detach_owned_activity(
    ActivityBindingState& binding) noexcept {
    const state::activity::ActivityInstanceKey owned =
        owns_published_activity(binding) ? binding.ownerLease.activity
                                         : state::activity::ActivityInstanceKey{};
    binding = ActivityBindingState{};
    return owned;
}

/**
 * Computes the lease installed with a successor binding.
 * Allocations create ownership; joins borrow unless they bind the exact already-owned lifetime.
 */
[[nodiscard]] inline ActivityRecordLease lease_after_replacement(
    const ActivityBindingState& previous,
    state::activity::ActivityInstanceKey next,
    ActivityBindingOrigin origin) noexcept {
    if (!static_cast<bool>(next)) {
        return {};
    }
    if (origin == ActivityBindingOrigin::allocation) {
        return {next, ActivityRecordCreator::bapService6};
    }
    return owns_published_activity(previous) && previous.instance == next
               ? previous.ownerLease
               : ActivityRecordLease{};
}

/**
 * Selects an earlier BAP lease that must retire after its successor is assigned.
 * An allocation's exact atomic replacement was already consumed by the State commit.
 */
[[nodiscard]] inline state::activity::ActivityInstanceKey lease_to_retire_after_replacement(
    const ActivityBindingState& previous,
    state::activity::ActivityInstanceKey next,
    ActivityBindingOrigin origin,
    state::activity::ActivityInstanceKey atomicReplacement) noexcept {
    if (!owns_published_activity(previous)) {
        return {};
    }
    if (origin == ActivityBindingOrigin::join && previous.instance == next) {
        return {};
    }
    if (origin == ActivityBindingOrigin::allocation
        && atomicReplacement == previous.ownerLease.activity) {
        return {};
    }
    return previous.ownerLease.activity;
}

/** Stages the next roster-publication generation without publishing its binding-owned clock. */
[[nodiscard]] inline bool stage_roster_publication_generation(
    const ActivityBindingState& binding,
    state::activity::PublicationGeneration& output) noexcept {
    output = {};
    if (!static_cast<bool>(binding.key) || !static_cast<bool>(binding.instance)
        || binding.rosterPublicationGenerationExhausted) {
        return false;
    }
    state::activity::PublicationGeneration candidate = binding.rosterPublicationClock;
    bool exhausted = false;
    if (!state::activity::advance(candidate, exhausted)) {
        return false;
    }
    output = candidate;
    return true;
}

/** @return True only while a staged roster still belongs to the current exact binding. */
[[nodiscard]] inline bool staged_roster_is_current(const ActivityBindingState& binding) noexcept {
    const RosterPublication& staged = binding.rosterStaged;
    return staged.staged && static_cast<bool>(binding.key)
           && static_cast<bool>(binding.instance) && static_cast<bool>(staged.binding)
           && static_cast<bool>(staged.activity) && static_cast<bool>(staged.publication)
           && staged.binding == binding.key && staged.activity == binding.instance
           && (staged.hasAfter
                   ? binding.rosterPublicationClock.value
                             != state::activity::kMaximumGeneration
                         && staged.publication.value
                                == binding.rosterPublicationClock.value + 1U
                   : staged.publication == binding.rosterPublicationClock);
}

/** Computes the sole role bit preserved across whole-value binding replacement. */
[[nodiscard]] inline bool joined_foreign_session_after_replacement(
    const ActivityBindingState& previous,
    state::activity::ActivityInstanceKey nextActivity,
    bool fromJoin) noexcept {
    if (!fromJoin) {
        return false;
    }
    return previous.instance == nextActivity ? previous.joinedForeignSession : true;
}

} // namespace lifecycle

/**
 * Securely retires everything owned by the current authentication lifetime.
 * The connection key and its connection-local authentication allocator are preserved.
 */
void reset_auth_owned_state(Session& session) noexcept;

/**
 * Retires one BAP-owned activity and every group-host record derived from it.
 * The serialized BAP lock must already be held. Repeating the call is safe.
 */
void retire_bap_activity_lease_locked(state::activity::ActivityInstanceKey activity) noexcept;

/** True only while some authenticated current BAP binding owns this exact creator record. */
[[nodiscard]] bool has_current_bap_creator_locked(
    state::activity::ActivityInstanceKey source) noexcept;

/** Resolves an allocation/join binding's exact creator-root proof before State commit. */
[[nodiscard]] bool prepare_region_lineage_locked(
    const Session& session,
    state::activity::ActivityInstanceKey next,
    bool fromJoin,
    RegionLineage& output,
    gameplay::group::HostActivityLineageLease& groupLease) noexcept;

/** Revalidates a binding's exact target/source edge and current creator owner. */
[[nodiscard]] bool region_lineage_is_current_locked(
    const Session& session,
    const RegionLineage& lineage) noexcept;

/**
 * Acquires and retains the exact borrowed bound row while proving its creator owner.
 * Owned lineages need no group pin; a successful borrowed result leaves `lease` pinned.
 */
[[nodiscard]] bool acquire_region_lineage_locked(
    const Session& session,
    const RegionLineage& lineage,
    gameplay::group::HostActivityLineageLease& lease) noexcept;

/** Revalidates an owned lineage or an already-retained borrowed-row proof. */
[[nodiscard]] bool retained_region_lineage_is_current_locked(
    const Session& session,
    const RegionLineage& lineage,
    const gameplay::group::HostActivityLineageLease& lease) noexcept;

/** Publishes a diagnostic anchor only for a committed exact creator-owned region report. */
void publish_hud_region_locked(Session& session,
                               state::activity::HostRegionKey hostRegion) noexcept;

/** Updates or clears the diagnostic anchor after whole-value binding replacement. */
void update_hud_anchor_after_binding_replacement_locked(
    const ActivityBindingState& previous,
    const ActivityBindingState& next) noexcept;

namespace plaintext {

/**
 * Handles plaintext bootstrap services, arms encryption after service 25, and routes the rest.
 * @param session Auth and nonce state owned by the connection.
 * @param scratch Transform buffers owned by the lock, kept off the Client thread stack.
 * @param outer Parsed outer frame carrying the service id and its body.
 * @param response Whole-frame storage owned by the caller.
 * @param written Gets the encoded response size in bytes.
 * @return True when the service owes no reply, or its response is encoded.
 */
[[nodiscard]] bool consume(Session& session,
                           Scratch& scratch,
                           const middleware::bap::OuterFrame& outer,
                           std::span<std::byte> response,
                           std::size_t& written) noexcept;

} // namespace plaintext

namespace encrypted {

/**
 * Authenticates and routes one encrypted post-bootstrap service frame.
 * @param session Auth and nonce state owned by the connection.
 * @param scratch Transform buffers owned by the lock, kept off the Client thread stack.
 * @param outer Validated encrypted outer frame.
 * @param response Whole-frame storage owned by the caller.
 * @param written Gets the encoded response size in bytes.
 * @return True when routing works, any response fits, State commits and the nonce is published.
 */
[[nodiscard]] bool consume(Session& session,
                           Scratch& scratch,
                           const middleware::bap::OuterFrame& outer,
                           std::span<std::byte> response,
                           std::size_t& written) noexcept;

/**
 * Sends the owed Family-4 re-push once its delay has passed.
 * @param session Auth, nonce and queuez state owned by the connection.
 * @param scratch Transform buffers owned by the lock, kept off the Client thread stack.
 * @param response Whole-frame storage owned by the caller.
 * @param written Gets the encoded notification size in bytes.
 * @param touchesScratch Set before any scratch buffer is used.
 * @return True when a whole Family-4 notification is published.
 */
[[nodiscard]] bool consume_deferred(Session& session,
                                    Scratch& scratch,
                                    std::span<std::byte> response,
                                    std::size_t& written,
                                    bool& touchesScratch) noexcept;

} // namespace encrypted

} // namespace dawn::server::bap
