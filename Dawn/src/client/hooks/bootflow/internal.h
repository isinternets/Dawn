#pragma once

#include <cstddef>
#include <cstdint>

#include "../../patterns/image_scan.h"

namespace dawn::client::hooks::bootflow {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * Holds the client-wide late-call admission lock for one retail-log-triggered bootflow operation.
 * Fresh lifecycle installation bypasses this guard on its owning thread; every other caller is
 * rejected after quiesce closes admission.
 */
class LateInstallGuard final {
public:
    LateInstallGuard() noexcept;
    ~LateInstallGuard() noexcept;

    LateInstallGuard(const LateInstallGuard&) = delete;
    LateInstallGuard& operator=(const LateInstallGuard&) = delete;

    /** @return True when this operation belongs to the current accepting client lifecycle. */
    [[nodiscard]] bool accepted() const noexcept;

private:
    bool ownsLock_{};
    bool accepted_{};
};

/**
 * Last type-43 authoritative-state application observed for Omega's opening scene.
 *
 * The scene-authority consumer and the native scene scheduler live in separate translation units.
 * This bounded value copy lets the scheduler recorder correlate one scene initialization with the
 * exact server-decoded state that preceded it, without retaining or dereferencing diagnostic
 * pointers after their owning callback returns.
 */
struct OmegaSceneAuthorityObservation final {
    std::uint64_t tickMs{};
    std::uint64_t sourceHash{};
    std::uintptr_t component{};
    std::uintptr_t datumKey{};
    std::uintptr_t source{};
    std::uint32_t sequence{};
    std::uint32_t datumIdentity{};
    std::uint32_t source00{};
    std::uint32_t source04{};
    std::uint32_t source08{};
    std::uint32_t source4C{};
    std::uint32_t source98{};
    std::uint32_t componentValueBefore{};
    std::uint32_t componentValueAfter{};
    std::uint8_t componentActiveBefore{};
    std::uint8_t componentActiveAfter{};
    bool sourceValid{};
};

/** Copies the most recent Omega type-43 authority application, if one has occurred. */
[[nodiscard]] bool snapshot_omega_scene_authority_observation(
    OmegaSceneAuthorityObservation& observation) noexcept;

/**
 * Attaches the character-select hold, which stops the sign-in step auto-selecting.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_character_select_hold() noexcept;

/** Detaches the character-select hold. */
void uninstall_character_select_hold() noexcept;

/**
 * Asks for the character-select hold to be released at the next opportunity.
 * A character created from the sign-in step is selected as it is created, and the step leaves for
 * the game only once the hold is off. The release waits for the next request the Client makes, so
 * the Client has already taken in the account that names its new selection.
 */
void request_character_select_release() noexcept;

/** Releases the hold when one was requested. Cheap when none was, so every request may call it. */
void apply_character_select_release() noexcept;

/**
 * Attaches the profile-setup skip, which skips the startup setup screens.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_profile_setup_skip() noexcept;

/** Detaches the profile-setup skip. */
void uninstall_profile_setup_skip() noexcept;

/**
 * Attaches the orbit slice-set picker, so the sign-in step's map load finds its target.
 * @return True when the picker is found and the detour attaches.
 */
[[nodiscard]] bool install_orbit_slice_set() noexcept;

/** Detaches the orbit slice-set picker. */
void uninstall_orbit_slice_set() noexcept;

/**
 * Attaches the solo composition fix, which clears the count the matchmaking check rejects.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_composition_check() noexcept;

/** Detaches the solo composition fix. */
void uninstall_composition_check() noexcept;

/**
 * Attaches the orbit handoff release, which stops the destination step parking.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_orbit_handoff() noexcept;

/** Detaches the orbit handoff release. */
void uninstall_orbit_handoff() noexcept;

/**
 * Attaches the join-request readiness force, which moves the activity session to status 6.
 * Two of the gate's five terms are client flags with no host input.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_join_request_ready() noexcept;

/** Detaches the join-request readiness force. */
void uninstall_join_request_ready() noexcept;

/**
 * Attaches the owner activity slot force. It pins the participation record to the replicated
 * snapshot at `comp + 496` instead of the local one at `comp + 1256`.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_owner_activity_slot() noexcept;

/** Detaches the owner activity slot force. */
void uninstall_owner_activity_slot() noexcept;

/**
 * Attaches the scoped Homecoming decoded bubble-host assignment bridge.
 * @return True when the message-54 apply target is found and detoured.
 */
[[nodiscard]] bool install_activity_host_assignment() noexcept;

/** Detaches the decoded bubble-host assignment bridge. */
void uninstall_activity_host_assignment() noexcept;

/**
 * Legacy activity-selection probe. Quarantined because callbacks can publish native selection.
 * @return True when the request-update target is found and the detour attaches.
 */
[[nodiscard]] bool install_activity_selection_probe() noexcept;

/** @return True if any quarantined activity-selection detour is unexpectedly attached. */
[[nodiscard]] bool activity_selection_probe_attached() noexcept;

/** Detaches the activity-selection probe. */
void uninstall_activity_selection_probe() noexcept;

/** Attaches the read-only global-activity-state type-1 client apply observer. */
[[nodiscard]] bool install_activity_notification_type1_apply_probe() noexcept;

/** Detaches the global-activity-state type-1 client apply observer. */
void uninstall_activity_notification_type1_apply_probe() noexcept;

/** Observation-only probe over the type-53 dialogue apply/scan/dispatch chain. */
[[nodiscard]] bool install_omega_dialogue_dispatch_probe() noexcept;
// Available only after the exact DF6BD0 signature was verified and hooked.
// Arc receipts quiesce before the dialogue probe releases this trampoline.
void* omega_native_device_channel0() noexcept;
void uninstall_omega_dialogue_dispatch_probe() noexcept;
[[nodiscard]] bool install_omega_navigation() noexcept;
void dispatch_hijacked_boss_teleport() noexcept;
[[nodiscard]] bool install_omega_lair_cinematic() noexcept;
void quiesce_omega_lair_cinematic() noexcept;
[[nodiscard]] bool uninstall_omega_lair_cinematic() noexcept;
[[nodiscard]] bool install_omega_enemy_lair_receipts() noexcept;
void quiesce_omega_enemy_lair_receipts() noexcept;
[[nodiscard]] bool uninstall_omega_enemy_lair_receipts() noexcept;
[[nodiscard]] bool install_omega_first_cannon_receipt() noexcept;
void quiesce_omega_first_cannon_receipt() noexcept;
[[nodiscard]] bool uninstall_omega_first_cannon_receipt() noexcept;
void quiesce_omega_navigation() noexcept;
[[nodiscard]] bool uninstall_omega_navigation() noexcept;


/** Legacy player_broadcast probe. Quarantined because it performs a native timing correction. */
[[nodiscard]] bool install_player_broadcast_create_probe() noexcept;

/** @return True if any quarantined player-broadcast timing-correction detour is attached. */
[[nodiscard]] bool player_broadcast_create_probe_attached() noexcept;

/** Detaches the player_broadcast creation observer. */
void uninstall_player_broadcast_create_probe() noexcept;

/** Arms a one-shot read-only dump of the selection manager consumed by the next launch. */
void arm_activity_selection_launch_state_probe() noexcept;

/**
 * Attaches the read-only archived activity-script event observer.
 * @return True when the exact pinned-client callback is validated and detoured.
 */
[[nodiscard]] bool install_activity_script_event_probe() noexcept;

/** @return True if any quarantined legacy activity-script event observer is attached. */
[[nodiscard]] bool activity_script_event_probe_attached() noexcept;

/** Detaches the archived activity-script event observer. */
void uninstall_activity_script_event_probe() noexcept;

/** Legacy script-upstream group. Quarantined because it can alter authority and manager stage. */
[[nodiscard]] bool install_activity_script_upstream_probe() noexcept;

/** @return True if any quarantined script authority/stage detour is unexpectedly attached. */
[[nodiscard]] bool activity_script_upstream_probe_attached() noexcept;

/** Detaches the activity-script upstream update observers. */
void uninstall_activity_script_upstream_probe() noexcept;

/** Arms a bounded, observe-only embedded-route trace when Omega script state 4 lands. */
void arm_omega_forest_route_trace() noexcept;

/** Guards the late-zone activity metadata evaluator from a stale -1 reverse mapping. */
[[nodiscard]] bool install_activity_provider_stale_mapping_guard() noexcept;

/** Detaches the scoped stale activity-provider mapping guard. */
void uninstall_activity_provider_stale_mapping_guard() noexcept;

/** Preserve native destruction when its optional sibling-owner lookup is empty. */
[[nodiscard]] bool install_native_cleanup_owner_guard() noexcept;
[[nodiscard]] bool install_native_property_list_guard() noexcept;
[[nodiscard]] bool install_local_reconnect() noexcept;
void quiesce_local_reconnect() noexcept;
[[nodiscard]] bool uninstall_local_reconnect() noexcept;
void quiesce_native_property_list_guard() noexcept;
[[nodiscard]] bool uninstall_native_property_list_guard() noexcept;
void quiesce_native_cleanup_owner_guard() noexcept;
[[nodiscard]] bool uninstall_native_cleanup_owner_guard() noexcept;

/** Attaches read-only Omega mission-runner and behaviour-condition diagnostics. */
[[nodiscard]] bool install_activity_behavior_condition_probe() noexcept;

/** Detaches the Omega mission-runner and behaviour-condition diagnostics. */
void uninstall_activity_behavior_condition_probe() noexcept;

/** Attaches read-only observers to Omega's exact native AI-spawner request chain. */
[[nodiscard]] bool install_activity_spawner_chain_probe() noexcept;

/** Detaches the native AI-spawner request-chain observers. */
void uninstall_activity_spawner_chain_probe() noexcept;

/** Always observes New Light's shutters; additional Omega hooks require their experiment settings. */
[[nodiscard]] bool install_omega_ikora_origin_probe() noexcept;

/** Detaches the shared factory and any installed optional Omega hooks. */
[[nodiscard]] bool uninstall_omega_ikora_origin_probe() noexcept;

/** Stops shared factory and optional Omega work while native forwarding remains live. */
void quiesce_omega_ikora_origin_probe() noexcept;

/** Installs Omega's post-arrival, native C252E306 New Objective presentation edge. */
[[nodiscard]] bool install_omega_directive_presentation() noexcept;

/** Attempts and confirms the bounded presentation edge from the main game thread. */
void sample_omega_directive_presentation() noexcept;

/** Detaches and resets the Omega objective presentation edge. */
[[nodiscard]] bool uninstall_omega_directive_presentation() noexcept;

/** Stops new directive capture/presentation work while the producer is being detached. */
void quiesce_omega_directive_presentation() noexcept;

/** Attaches the read-only terminal retirement observer for Omega's authored scene casts. */
[[nodiscard]] bool install_omega_scene_retirement_probe() noexcept;

/** Detaches the narrow authored scene-retirement observer. */
[[nodiscard]] bool uninstall_omega_scene_retirement_probe() noexcept;

/** Stops new Scene-retirement observation work while native forwarding remains live. */
void quiesce_omega_scene_retirement_probe() noexcept;

/** Captures the six activity-script manager slots at a known boot-flow handoff. */
void observe_activity_script_manager_table() noexcept;

/** Installs the sole default-off, observation-only Type-31 +B20640 owner. */
[[nodiscard]] bool install_type31_objective_capture() noexcept;

/** Stops new Type-31 capture/drain work while native forwarding remains live. */
void quiesce_type31_objective_capture() noexcept;

/** Protected detach; failed or active removal retains handle, trampoline, epoch, and queue. */
[[nodiscard]] bool uninstall_type31_objective_capture() noexcept;

/** Drains a fixed Type-31 budget on the installed game-thread poll. */
void sample_type31_objective_capture() noexcept;
/** Captures the native CHOSEN descriptor and arms Homecoming's authored-launch producer. */
void notify_homecoming_authored_selection(const std::byte* descriptor) noexcept;
void attempt_activity_script_bootstrap() noexcept;
/** Signals that Destiny finished activity-host setup and entered the forced mission prologue. */
void notify_activity_script_setup_complete() noexcept;
/** Captures the verified activity client accepted by Destiny's native join-result path. */
void notify_activity_script_client_joined(std::byte* activityClient) noexcept;
/** Signals that Destiny is fully in-world and has started the forced mission activity. */
void notify_activity_script_world_started() noexcept;
void reset_activity_script_bootstrap() noexcept;

/**
 * Attaches the read-only authority-schema decoder observer.
 * @return True when the exact pinned-client decoder is validated and detoured.
 */
[[nodiscard]] bool install_activity_schema_decode_probe() noexcept;

/** @return True if the quarantined 18-site legacy schema bundle retains owner state. */
[[nodiscard]] bool activity_schema_decode_legacy_bundle_has_ownership() noexcept;

/** Detaches the authority-schema decoder observer. */
void uninstall_activity_schema_decode_probe() noexcept;

/** Samples Omega's cached scene/gate helper runtimes for local, non-wire state transitions. */
void sample_omega_opening_runtime_state() noexcept;

/**
 * Observes the activity feature-flag getter (a global byte gating the solo activity-script init that
 * enables identities) and, under the Homecoming/towerfall override, returns 1 to the solo-init's read
 * only, to test whether opening that gate wakes the authored-enable path.
 * @return True when the exact getter is validated and detoured.
 */
[[nodiscard]] bool install_activity_feature_flag_probe() noexcept;

/** @return True if the quarantined feature-flag detour is unexpectedly attached. */
[[nodiscard]] bool activity_feature_flag_probe_attached() noexcept;

/** Detaches the activity feature-flag observer. */
void uninstall_activity_feature_flag_probe() noexcept;

/**
 * Arms a bounded, observation-only trace for the native decoder that consumes one service-7
 * activity-host-manager response. The caller retains ownership of the response bytes.
 */
void arm_activity_host_manager_response_decode_probe(std::uint64_t sessionId,
                                                     const std::byte* response,
                                                     std::size_t responseSize) noexcept;

/** Attaches read-only observers to the group membership and parameter-update decoders. */
[[nodiscard]] bool install_group_initial_update_decode_probe() noexcept;

/** Detaches the group initial-update decoder observers. */
void uninstall_group_initial_update_decode_probe() noexcept;

/** Dynamically observes the three virtual methods used by the state-2 secure-channel update. */
[[nodiscard]] bool install_secure_channel_predicate_probe(void* channelObject) noexcept;

/** Detaches the dynamically discovered secure-channel method observers. */
void uninstall_secure_channel_predicate_probe() noexcept;

/**
 * Attaches the scoped Homecoming local prologue-filler readiness force. The retired activity
 * authority never sets its completion bit, so the initial-slice state otherwise waits forever.
 * @return True when the readiness accessor is found and the detour attaches.
 */
[[nodiscard]] bool install_prologue_filler_ready() noexcept;

/** Clears the Homecoming prologue force before a new initial slice starts loading. */
void reset_prologue_filler_ready() noexcept;

/** Arms the Homecoming prologue force after the initial slice transition completes normally. */
void arm_prologue_filler_ready() noexcept;

/** @return True after Destiny completes the real initial-slice transition for this launch. */
[[nodiscard]] bool prologue_filler_ready_armed() noexcept;

/**
 * Attaches the private-region force, so a public region takes the path a private one takes.
 * A public region otherwise holds its slice-set switch until a public activity host connects.
 * @return True when both targets are found, the call site is unique and the detour attaches.
 */
[[nodiscard]] bool install_region_private() noexcept;

/** Detaches the private-region force. */
void uninstall_region_private() noexcept;

/**
 * Finds the boot-flow step accessor, the only input to the world phase.
 * Nothing is detoured: the accessor is called, so a miss leaves the phase idle.
 * @return True when the target was found.
 */
[[nodiscard]] bool install_world_step() noexcept;

/** Clears the boot-flow step accessor it found. */
void uninstall_world_step() noexcept;

/**
 * Maps the client's own boot-flow step onto the world phase.
 * Runs on the spawn gate poll, which is the only tick the phase is read on.
 */
void observe_world_step() noexcept;

/** Completes arrival from the camera frame after an early spawn stops polling its gate. */
void poll_spawn_arrival() noexcept;

/**
 * Attaches the spawn hold, which puts the player spawn after the world-transition fade is armed.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_spawn_hold() noexcept;

/** Stops Dawn-owned spawn work while retaining native forwarding. */
void quiesce_spawn_hold() noexcept;

/** Detaches the spawn hold, retaining all owner state unless removal is confirmed. */
[[nodiscard]] bool uninstall_spawn_hold() noexcept;

/**
 * Attaches the narrow native launch-producer/manager-dispatch owner used by Towerfall.
 * Normal launches are observed unchanged so their successful producer chain remains a reference.
 */
[[nodiscard]] bool install_towerfall_executor_bootstrap() noexcept;

/** Arms manager-update retries after Towerfall reaches its validated native in-world boundary. */
void arm_towerfall_executor_bootstrap() noexcept;

/** Stops new producer retries while retaining native forwarding. */
void quiesce_towerfall_executor_bootstrap() noexcept;

/** Detaches the focused producer and manager observers. */
[[nodiscard]] bool uninstall_towerfall_executor_bootstrap() noexcept;

/**
 * Finds the world-transition fade release and its manager object.
 * Nothing is detoured: both are called, so a miss leaves the feature off, not the client changed.
 * @return True when both targets were found.
 */
[[nodiscard]] bool install_fade_release() noexcept;

/** Clears the fade release it found. */
void uninstall_fade_release() noexcept;

/**
 * Releases the world-transition fade channel.
 * The spawn gate owns the timing. Does nothing unless `client.fade_release` is set.
 */
void release_world_fade(bool flyInComplete=false) noexcept;
void poll_opening_fade() noexcept;

/** Re-arms the one line the release logs, so the next load reports its own. */
void rearm_fade_release() noexcept;

} // namespace dawn::client::hooks::bootflow
