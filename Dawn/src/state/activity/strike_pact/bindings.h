// Native identifiers and allowed operations for the whole strike. Mission decisions live in
// scripts/strike_pact.lua. Every asset here is an identity out of a section catalog; this file
// only says which of them the Lua may name and what each name is allowed to do.
#pragma once
#include "../coo/mission_script.h"
#include "catalog_all.h"
#include "catalog_presentation.h"
namespace dawn::state::activity::strike_pact {

inline constexpr coo::Asset kModule{kRoot,kScenario,0,0};
inline constexpr coo::Asset kDialogueAsset{kRoot,kRootTag,53,2};
inline constexpr coo::Asset kObjectiveAsset{kRoot,kRootTag,68,0};
inline constexpr coo::Asset kPortalAsset{kTeleport,kTeleportTag,4,0};
inline constexpr coo::Asset kForestPortalPoint{kLighthouse,kLighthouseTag,47,8};
inline constexpr coo::Asset kHarvesterAsset{kLedge,kLedgeTag,kHarvesterPilotType,kHarvesterPilot};

// A held region is not a place the player can stand in, so it cannot be a volume. The mission
// still has to wait for one, because every section is entered through a native portal or fade
// rather than by walking. These observations carry a reserved asset and the region index as their
// argument; the controller answers them from the region the composition reports.
inline constexpr coo::Asset kRegionAsset{0xFFFFFFFEU,0xFFFFFFFEU,0,0};
// The same reservation for the mission's own retained latches, which no native object owns.
inline constexpr coo::Asset kMissionAsset{0xFFFFFFFDU,0xFFFFFFFDU,0,0};

// Frame bits for the authored devices the mission takes authority over.
inline constexpr std::uint32_t kDeviceGatewayShield=1U<<0;
inline constexpr std::uint32_t kDeviceForestShield=1U<<1;
inline constexpr std::uint32_t kDeviceChaseLasers=1U<<2;

// Mechanic arguments. Each is one retained decision the mission makes, not a native object.
inline constexpr std::uint32_t kMechanicModule=1;
inline constexpr std::uint32_t kMechanicMarkerClear=40;
inline constexpr std::uint32_t kMechanicGenerator=41;
inline constexpr std::uint32_t kMechanicShipArrival=42,kMechanicShipDelivery=43,
    kMechanicShipDeparture=44,kMechanicShipRetire=45;
inline constexpr coo::Asset kBossMechanic{kBoss,kBossTag,2,kBossActor};
inline constexpr std::uint32_t kBossBegin=46,kBossRoom1Start=47,kBossRoom2Start=48,
    kBossRoom3Start=49,kBossRetreat2=50,kBossRetreat3=51,kBossDeath=52,
    kBossRoom1Exit=53,kBossRoom2Exit=54,kBossMinotaurSpawned=55,kBossMinotaurDead=56;
inline constexpr std::uint32_t kMechanicCheckpointBase=64;   // + the region's own index
/** One checkpoint the mission can select, as a mechanic argument the controller decodes. */
[[nodiscard]] constexpr std::uint32_t checkpoint_argument(std::size_t index) noexcept {
    return kMechanicCheckpointBase+static_cast<std::uint32_t>(index);
}

inline constexpr coo::script::Capability kCapabilities[]{
    {"scan.arm","boss",{coo::Operation::mechanic,kMissionAsset,80U,coo::Wait::requested}},
    {"scan.started","boss",{coo::Operation::observation,kMissionAsset,81U,coo::Wait::observed}},
    {"scan.complete","boss",{coo::Operation::observation,kMissionAsset,82U,coo::Wait::observed}},
    {"dialogue.scan","boss",{coo::Operation::dialogue,kDialogueAsset,29U,coo::Wait::requested}},
    {"dialogue.scan.finished","boss",{coo::Operation::observation,kDialogueAsset,29U,coo::Wait::observed}},

    {"forest.first_complete","*",{coo::Operation::observation,{kForest,kForestTag,37,kMapGenerator},0U,coo::Wait::observed}},
    {"dialogue.forest_first","*",{coo::Operation::dialogue,kDialogueAsset,6U,coo::Wait::requested}},
    {"boss.reveal","*",{coo::Operation::mechanic,kBossMechanic,kBossBegin,coo::Wait::completed}},
    {"boss.fight1","*",{coo::Operation::mechanic,kBossMechanic,kBossRoom1Start,coo::Wait::requested}},
    {"boss.fight2","*",{coo::Operation::mechanic,kBossMechanic,kBossRoom2Start,coo::Wait::requested}},
    {"boss.fight3","*",{coo::Operation::mechanic,kBossMechanic,kBossRoom3Start,coo::Wait::requested}},
    {"boss.room2.entered","*",{coo::Operation::observation,kBossMechanic,kBossRoom2Start,coo::Wait::observed}},
    {"boss.room3.entered","*",{coo::Operation::observation,kBossMechanic,kBossRoom3Start,coo::Wait::observed}},
    {"boss.retreat2","*",{coo::Operation::observation,kBossMechanic,kBossRetreat2,coo::Wait::observed}},
    {"boss.retreat3","*",{coo::Operation::observation,kBossMechanic,kBossRetreat3,coo::Wait::observed}},
    {"boss.exit1","*",{coo::Operation::observation,kBossMechanic,kBossRoom1Exit,coo::Wait::observed}},
    {"boss.exit2","*",{coo::Operation::observation,kBossMechanic,kBossRoom2Exit,coo::Wait::observed}},
    {"boss.minotaur.spawned","*",{coo::Operation::observation,kBossMechanic,kBossMinotaurSpawned,coo::Wait::observed}},
    {"boss.minotaur.dead","*",{coo::Operation::observation,kBossMechanic,kBossMinotaurDead,coo::Wait::observed}},
    {"boss.death","*",{coo::Operation::observation,kBossMechanic,kBossDeath,coo::Wait::observed}},
    {"opening.module","composition",{coo::Operation::mechanic,kModule,kMechanicModule,coo::Wait::requested}},
    {"opening.checked","composition",{coo::Operation::observation,{0,0,0,0},0U,coo::Wait::observed}},

    // ---- regions -------------------------------------------------------------------------
    {"region.opening","*",{coo::Operation::observation,kRegionAsset,120U,coo::Wait::observed}},
    {"region.forest","*",{coo::Operation::observation,kRegionAsset,72U,coo::Wait::observed}},
    {"region.chase","*",{coo::Operation::observation,kRegionAsset,16U,coo::Wait::observed}},
    {"region.ledge","*",{coo::Operation::observation,kRegionAsset,0U,coo::Wait::observed}},

    // ---- opening -------------------------------------------------------------------------
    {"landing.entered","*",{coo::Operation::observation,{kOpening,kOpeningTag,60,73},0U,coo::Wait::observed}},
    {"gate.entered","*",{coo::Operation::observation,{kOpening,kOpeningTag,60,71},0U,coo::Wait::observed}},
    {"tunnel.entered","*",{coo::Operation::observation,{kLighthouse,kLighthouseTag,60,5},0U,coo::Wait::observed}},
    {"teleported.entered","*",{coo::Operation::observation,{kLighthouse,kLighthouseTag,60,11},0U,coo::Wait::observed}},
    {"opening.stage1","*",{coo::Operation::population,kModule,opening_cohort(1),coo::Wait::requested}},
    {"opening.stage2","*",{coo::Operation::population,kModule,opening_cohort(2),coo::Wait::requested}},
    {"opening.stage3","*",{coo::Operation::population,kModule,opening_cohort(3),coo::Wait::requested}},
    {"gateway.cleared","*",{coo::Operation::population,kModule,opening_cohort(3),coo::Wait::completed}},
    {"shield.raise","*",{coo::Operation::device,{kOpening,kOpeningTag,23,kShieldWall},1U,coo::Wait::requested}},
    {"shield.drop","*",{coo::Operation::device,{kOpening,kOpeningTag,23,kShieldWall},0U,coo::Wait::requested}},
    {"portal.activate","*",{coo::Operation::device,kPortalAsset,1U,coo::Wait::requested}},
    {"objective.approach","*",{coo::Operation::objective,kObjectiveAsset,kApproachGateway,coo::Wait::requested}},
    {"objective.traverse","*",{coo::Operation::objective,kObjectiveAsset,kTraverseForest,coo::Wait::requested}},
    {"marker.clear","*",{coo::Operation::mechanic,kObjectiveAsset,kMechanicMarkerClear,coo::Wait::requested}},
    {"dialogue.intro","*",{coo::Operation::dialogue,kDialogueAsset,0U,coo::Wait::requested}},
    {"dialogue.scene_finished","*",{coo::Operation::dialogue,kDialogueAsset,21U,coo::Wait::requested}},
    {"dialogue.retreat2","*",{coo::Operation::dialogue,kDialogueAsset,24U,coo::Wait::requested}},
    {"objective.evade","*",{coo::Operation::objective,kObjectiveAsset,kEvadeVex,coo::Wait::requested}},
    {"dialogue.gate","*",{coo::Operation::dialogue,kDialogueAsset,1U,coo::Wait::requested}},
    {"dialogue.tunnel","*",{coo::Operation::dialogue,kDialogueAsset,4U,coo::Wait::requested}},

    // ---- Infinite Forest B ----------------------------------------------------------------
    // The generator is the whole section: without its activation the worker places no encounter
    // and the islands are empty, which is what an unactivated Forest looks like in game.
    {"forest.generate","*",{coo::Operation::mechanic,{kForest,kForestTag,37,kMapGenerator},kMechanicGenerator,coo::Wait::requested}},
    {"forest.shield.raise","*",{coo::Operation::device,{kForest,kForestTag,23,kForestShieldWall},1U,coo::Wait::requested}},
    {"forest.shield.drop","*",{coo::Operation::device,{kForest,kForestTag,23,kForestShieldWall},0U,coo::Wait::requested}},
    {"forest.defense","*",{coo::Operation::population,kModule,forest_cohort(1),coo::Wait::requested}},
    {"forest.cleared","*",{coo::Operation::population,kModule,forest_cohort(1),coo::Wait::completed}},
    {"forest.load_post","*",{coo::Operation::observation,{kForest,kForestTag,60,45},0U,coo::Wait::observed}},
    {"forest.entered","*",{coo::Operation::observation,{kForest,kForestTag,60,46},0U,coo::Wait::observed}},
    {"forest.started","*",{coo::Operation::observation,{kForestArea,kForestAreaTag,60,7},0U,coo::Wait::observed}},
    {"forest.portal.seen","*",{coo::Operation::observation,{kForestArea,kForestAreaTag,60,3},0U,coo::Wait::observed}},
    {"forest.portal.reached","*",{coo::Operation::observation,{kForestArea,kForestAreaTag,60,6},0U,coo::Wait::observed}},
    {"forest.leaving","*",{coo::Operation::observation,{kForestExit,kForestExitTag,60,2},0U,coo::Wait::observed}},
    {"objective.track","*",{coo::Operation::objective,kObjectiveAsset,kTrackCabal,coo::Wait::requested}},
    {"objective.barrier","*",{coo::Operation::objective,kObjectiveAsset,kDisableBarrier,coo::Wait::requested}},
    {"dialogue.forest","*",{coo::Operation::dialogue,kDialogueAsset,5U,coo::Wait::requested}},
    {"dialogue.vex","*",{coo::Operation::dialogue,kDialogueAsset,6U,coo::Wait::requested}},
    {"checkpoint.forest","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(0),coo::Wait::requested}},
    {"checkpoint.forest_exit","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(1),coo::Wait::requested}},

    // ---- the Chase -------------------------------------------------------------------------
    {"chase.entered","*",{coo::Operation::observation,{kChaseEntry,kChaseEntryTag,60,2},0U,coo::Wait::observed}},
    {"chase.sparrow","*",{coo::Operation::observation,{kChase,kChaseTag,60,138},0U,coo::Wait::observed}},
    {"chase.jump","*",{coo::Operation::observation,{kChaseTriggers,kChaseTriggersTag,60,11},0U,coo::Wait::observed}},
    {"chase.firstroom","*",{coo::Operation::population,kModule,chase_cohort(1),coo::Wait::requested}},
    {"chase.firstroom.cleared","*",{coo::Operation::population,kModule,chase_cohort(1),coo::Wait::completed}},
    {"chase.reinforcements","*",{coo::Operation::population,kModule,chase_cohort(2),coo::Wait::requested}},
    {"chase.reinforcements.cleared","*",{coo::Operation::population,kModule,chase_cohort(2),coo::Wait::completed}},
    {"chase.conflict","*",{coo::Operation::population,kModule,chase_cohort(3),coo::Wait::requested}},
    {"chase.lasers","*",{coo::Operation::device,{kChase,kChaseTag,4,kChaseLasers[0]},1U,coo::Wait::requested}},
    {"objective.eliminate","*",{coo::Operation::objective,kObjectiveAsset,kEliminateHostiles,coo::Wait::requested}},
    {"objective.mount_up","*",{coo::Operation::objective,kObjectiveAsset,kMountUp,coo::Wait::requested}},
    {"objective.find_leader","*",{coo::Operation::objective,kObjectiveAsset,kFindLeader,coo::Wait::requested}},
    {"dialogue.chase","*",{coo::Operation::dialogue,kDialogueAsset,10U,coo::Wait::requested}},
    {"dialogue.mount_up","*",{coo::Operation::dialogue,kDialogueAsset,11U,coo::Wait::requested}},
    {"dialogue.jump","*",{coo::Operation::dialogue,kDialogueAsset,13U,coo::Wait::requested}},
    {"checkpoint.chase","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(2),coo::Wait::requested}},
    {"checkpoint.chase_cleared","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(3),coo::Wait::requested}},

    // ---- the bomb ledge --------------------------------------------------------------------
    {"ledge.arrival","*",{coo::Operation::population,kModule,ledge_cohort(kCohortLedgeArrival),coo::Wait::requested}},
    {"ledge.cleared","*",{coo::Operation::population,kModule,ledge_cohort(kCohortLedgeArrival),coo::Wait::completed}},
    {"ledge.reinforcements","*",{coo::Operation::population,kModule,ledge_cohort(kCohortLedgeReinforcements),coo::Wait::requested}},
    {"ledge.final","*",{coo::Operation::observation,{kLedge,kLedgeTag,30,kLedgeSensors[1].slot},0U,coo::Wait::observed}},
    {"ship.arrive","*",{coo::Operation::mechanic,kHarvesterAsset,kMechanicShipArrival,coo::Wait::completed}},
    {"ship.deliver","*",{coo::Operation::mechanic,kHarvesterAsset,kMechanicShipDelivery,coo::Wait::completed}},
    {"ship.depart","*",{coo::Operation::mechanic,kHarvesterAsset,kMechanicShipDeparture,coo::Wait::completed}},
    {"ship.retire","*",{coo::Operation::mechanic,kHarvesterAsset,kMechanicShipRetire,coo::Wait::requested}},
    {"ledge.see_tree","*",{coo::Operation::observation,{kBossRoomPoints,kBossRoomPointsTag,60,3},0U,coo::Wait::observed}},
    {"objective.find_map","*",{coo::Operation::objective,kObjectiveAsset,kFindMap,coo::Wait::requested}},
    {"dialogue.ledge","*",{coo::Operation::dialogue,kDialogueAsset,17U,coo::Wait::requested}},
    {"dialogue.ledge_final","*",{coo::Operation::dialogue,kDialogueAsset,18U,coo::Wait::requested}},
    {"dialogue.see_tree","*",{coo::Operation::dialogue,kDialogueAsset,19U,coo::Wait::requested}},
    {"checkpoint.ledge","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(4),coo::Wait::requested}},

    // ---- Valus Thuun -----------------------------------------------------------------------
    {"boss.approached","*",{coo::Operation::observation,{kBossApproach,kBossApproachTag,60,4},0U,coo::Wait::observed}},
    {"boss.prefight","*",{coo::Operation::population,kModule,boss_cohort(kCohortPrefight),coo::Wait::requested}},
    {"boss.prefight.cleared","*",{coo::Operation::population,kModule,boss_cohort(kCohortPrefight),coo::Wait::completed}},
    {"boss.participants","*",{coo::Operation::population,kModule,boss_cohort(kCohortParticipants),coo::Wait::requested}},
    {"boss.room1","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom1),coo::Wait::requested}},
    {"boss.room1.cleared","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom1),coo::Wait::completed}},
    {"boss.room2","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom2),coo::Wait::requested}},
    {"boss.room2.cleared","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom2),coo::Wait::completed}},
    {"boss.room3","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom3),coo::Wait::requested}},
    {"boss.room3.cleared","*",{coo::Operation::population,kModule,boss_cohort(kCohortRoom3),coo::Wait::completed}},
    {"objective.defeat","*",{coo::Operation::objective,kObjectiveAsset,kDefeatThuun,coo::Wait::requested}},
    {"objective.access_map","*",{coo::Operation::objective,kObjectiveAsset,kAccessMap,coo::Wait::requested}},
    {"dialogue.reveal","*",{coo::Operation::dialogue,kDialogueAsset,20U,coo::Wait::requested}},
    {"dialogue.room1","*",{coo::Operation::dialogue,kDialogueAsset,22U,coo::Wait::requested}},
    {"dialogue.room2","*",{coo::Operation::dialogue,kDialogueAsset,25U,coo::Wait::requested}},
    {"dialogue.room3","*",{coo::Operation::dialogue,kDialogueAsset,26U,coo::Wait::requested}},
    {"dialogue.dead","*",{coo::Operation::dialogue,kDialogueAsset,27U,coo::Wait::requested}},
    {"checkpoint.boss","*",{coo::Operation::mechanic,kMissionAsset,checkpoint_argument(5),coo::Wait::requested}},
    {"mission.finish","*",{coo::Operation::complete,kModule,6U,coo::Wait::requested}},
};
inline constexpr coo::script::ModuleCapability kModules[]{{"opening",{kModule,kMechanicModule}}};
inline constexpr coo::script::FactCapability kFacts[]{{"opening.checked",0}};
// The four unused locator hashes must carry the packaged absent sentinel: a zero reads as a
// supplied locator and produces false origin candidates in the native marker selector.
inline constexpr coo::script::MarkerCapability kMarkers[]{
    {"map_terminal",{{0x89CAE96BU,0x80F474F2U,47,0},presentation::kAbsentLocator}},
    {"forest_portal",{kForestPortalPoint,{0x811C9DC5U,0x811C9DC5U,0x811C9DC5U,0x811C9DC5U}}},
    {"forest_barrier",presentation::marker(presentation::kNavPoints[1])},
    {"forest_exit",presentation::marker(presentation::kNavPoints[2])},
    {"chase_fight",presentation::marker(presentation::kNavPoints[3])},
    {"chase_exit",presentation::marker(presentation::kNavPoints[4])},
    {"boss_approach",presentation::marker(presentation::kNavPoints[5])},
    {"boss_room3",presentation::marker(presentation::kNavPoints[7])},
    {"thuun",{{kBoss,kBossTag,2,kBossActor},presentation::kAbsentLocator}},
};
inline constexpr coo::DialogueBinding kRouteDialogue[]{
    {{kLighthouse,kLighthouseTag,60,5},4,0},
    {{kForest,kForestTag,60,46},5,0},
    {{kForest,kForestTag,37,kMapGenerator},6,0},
    {{kChaseTriggers,kChaseTriggersTag,60,11},13,0},
    {{kBossRoomPoints,kBossRoomPointsTag,60,3},19,0},
};
// Campaign traversal uses the same native monitors with its own authored cues.
inline constexpr coo::DialogueBinding kCampaignRouteDialogue[]{
    {{kLighthouse,kLighthouseTag,60,5},5,0},
    {{kForestArea,kForestAreaTag,60,7},4,0},
    {{kChaseEntry,kChaseEntryTag,60,2},10,0},
    {{kBossApproach,kBossApproachTag,60,4},19,0},
};
inline constexpr coo::script::PresentationTable kPresentationTables[]{
    {"route",{coo::Schema::otherMissions,{},{},kRouteDialogue}},
    {"campaign_route",{coo::Schema::otherMissions,{},{},kCampaignRouteDialogue}},
};
inline constexpr coo::script::Profile kNativeBindings{"strike_pact.v1","otherMissions",
    coo::Schema::otherMissions,kCapabilities,kModules,kFacts,presentation::kDialogue,presentation::kObjectives,{},kPresentationTables,kMarkers};
} // namespace dawn::state::activity::strike_pact
