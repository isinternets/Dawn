#include "mission.h"
#include <bit>
#include <algorithm>
namespace dawn::state::activity::vanilla::homecoming {
namespace {
using coo::CommandSpec;using coo::Operation;using coo::Wait;
CommandSpec objective(std::size_t i) {return {Operation::objective,kDirectiveAsset,kObjectives[i],Wait::requested};}
// Speech queues at its gameplay beat; the previous cue's authored window
// prevents overlap. Optional delays hold requests, not mission progression.
CommandSpec speech(std::uint8_t row,std::uint32_t delayMs=0) {return {Operation::dialogue,kDialogueAsset,row|(delayMs<<8),Wait::requested};}
CommandSpec spoken(std::uint8_t row) {return {Operation::observation,kDialogueAsset,row,Wait::observed};}
CommandSpec speaking(std::uint8_t row) {return {Operation::observation,kDialogueAsset,kDialogueStarted|row,Wait::observed};}
CommandSpec visit(std::uint32_t r,std::string_view trigger) {return {Operation::observation,trigger_area(r,trigger),0,Wait::observed};}
CommandSpec area(std::uint32_t r,std::string_view volume) {return {Operation::observation,volume_named(r,volume),0,Wait::observed};}
CommandSpec reached(Milestone m) {return {Operation::observation,kModule,static_cast<std::uint32_t>(m),Wait::observed};}
CommandSpec after(std::uint32_t ms) {return {Operation::eventAfter,kModule,ms,Wait::observed};}
CommandSpec device(std::uint32_t r,std::uint16_t slot,float position) {return {Operation::device,asset(r,23,slot),std::bit_cast<std::uint32_t>(position),Wait::requested};}
CommandSpec posed(std::uint32_t r,std::uint16_t slot,float position) {return {Operation::observation,asset(r,23,slot),std::bit_cast<std::uint32_t>(position),Wait::observed};}
CommandSpec object(std::uint32_t r,std::uint16_t slot,bool active=true) {return {Operation::device,asset(r,4,slot),active?1U:0U,Wait::requested};}
CommandSpec present(std::uint32_t r,std::uint16_t slot) {return {Operation::observation,asset(r,4,slot),static_cast<std::uint32_t>(ObjectEvent::present),Wait::observed};}
CommandSpec used(std::uint32_t r,std::uint16_t slot) {return {Operation::observation,asset(r,4,slot),static_cast<std::uint32_t>(ObjectEvent::used),Wait::observed};}
CommandSpec sequence(std::uint32_t r,std::uint16_t slot) {return {Operation::device,asset(r,5,slot),1,Wait::requested};}
CommandSpec bind(std::uint32_t r,std::uint16_t slot) {return {Operation::device,asset(r,2,slot),1,Wait::requested};}
CommandSpec ghost(std::uint32_t r,std::uint16_t slot,bool active) {return {Operation::device,asset(r,65,slot),active?1U:0U,Wait::requested};}
CommandSpec scene(std::uint32_t r,std::uint16_t slot) {return {Operation::scene,asset(r,43,slot),0,Wait::requested};}
CommandSpec sceneEvent(std::uint32_t r,std::uint16_t slot,std::uint32_t key) {return {Operation::scene,asset(r,43,slot),key,Wait::requested};}
CommandSpec sceneObserved(std::uint32_t r,std::uint16_t slot,SceneEvent event=SceneEvent::started) {return {Operation::observation,asset(r,43,slot),static_cast<std::uint32_t>(event),Wait::observed};}
CommandSpec control(Mechanic value) {return {Operation::mechanic,kModule,static_cast<std::uint32_t>(value),Wait::requested};}
CommandSpec mechanic(coo::Asset a,Mechanic value) {return {Operation::mechanic,a,static_cast<std::uint32_t>(value),Wait::requested};}
CommandSpec music(std::uint8_t section) {return {Operation::mechanic,kMusicAsset,static_cast<std::uint32_t>(Mechanic::music)|(std::uint32_t{section}<<8),Wait::requested};}
CommandSpec batch(SpawnCheckpoint c) {return {Operation::population,kModule,static_cast<std::uint32_t>(c),Wait::requested};}
CommandSpec cleared(Cohort c) {return {Operation::observation,kModule,0x100U+static_cast<std::uint32_t>(c),Wait::observed};}
CommandSpec finish() {return control(Mechanic::finishSection);}
// Authored graph inputs (package evidence; see the audit's scene reviews).
inline constexpr std::uint32_t kEntry=0x6F51AC66U,kBreachEntry=0xAE7CC69CU,kIkoraMove=0x84FFD4F6U,kIkoraBlast=0x0B78A21AU,
    kZavalaArrive=0xB8C5C0A5U,kZavalaArriveB=0x1BED1ED3U,kZavalaWard=0xC021F76CU,kZavalaDeath=0xC021F76FU,
    kPostGunRelease=0x38857CF3U,kCoverCombatRelease=0x18EF2ABCU;
namespace ms=music_section;
}
void Graph::name(std::string_view label) noexcept {
    definition={label,coo::Schema::otherMissions,{},{}};
    first_=storage_?storage_->stepCount:0;
}
std::uint32_t Graph::add(std::string_view label,std::uint32_t dependencies,std::initializer_list<coo::CommandSpec> commands) noexcept {
    const auto size=definition.steps.size();
    if(!storage_ || definition.schema==coo::Schema::unspecified
        || size>=coo::Executor::kMaxSteps || commands.size()==0 || commands.size()>coo::Executor::kMaxCommands
        || storage_->stepCount!=first_+size || storage_->stepCount>=storage_->steps.size()
        || commands.size()>storage_->commands.size()-storage_->commandCount) {
        definition.schema=coo::Schema::unspecified;return 0;
    }
    auto target=std::span(storage_->commands).subspan(storage_->commandCount,commands.size());
    std::copy(commands.begin(),commands.end(),target.begin());storage_->commandCount+=commands.size();
    storage_->steps[storage_->stepCount++]={label,dependencies,target};
    definition.steps=std::span(storage_->steps).subspan(first_,size+1);return 1U<<size;
}
// Beat order follows the user's supplied retail transcript (2026-09-20).
// Travel/combat observations own progression; walkthrough elapsed timestamps
// are not mission timers. Native performers retain their embedded dialogue.
Mission::Mission() noexcept {
    for(auto& phase:phases) {phase.storage_=&storage;}
    // Underwatch. Every civilian pose and reaction, the frames firing at the Cabal ship and
    // the loose Red Guard cast are placed on landing so the ruins are populated before the
    // player looks around. Combat groups keep their individual route triggers so
    // later hallway enemies cannot converge on the opening fight before arrival.
    auto& underwatch=phases[static_cast<std::size_t>(Section::underwatch)];underwatch.name("Homecoming / Underwatch");
    auto arrival=underwatch.add("crash site",0,{objective(0),music(ms::underwatchRuins),batch(SpawnCheckpoint::hallway),device(kUnderwatch,108,0.F),
        object(kUnderwatch,71),object(kUnderwatch,72),object(kUnderwatch,81),speech(1)});
    underwatch.add("frames and civilian poses",0,{scene(kUnderwatch,5),scene(kUnderwatch,51),scene(kUnderwatch,52),scene(kUnderwatch,53),scene(kUnderwatch,54),scene(kUnderwatch,55),scene(kUnderwatch,83),scene(kUnderwatch,85)});
    underwatch.add("civilian reactions",0,{scene(kUnderwatch,87),scene(kUnderwatch,89),scene(kUnderwatch,91),scene(kUnderwatch,93),scene(kUnderwatch,95),scene(kUnderwatch,97),scene(kUnderwatch,99)});
    auto a=underwatch.add("wall approach",arrival,{visit(kUnderwatch,"pt_wall_explode")});
    auto b=underwatch.add("first contact scene",a,{music(ms::firstCabal),batch(SpawnCheckpoint::firstContact),scene(kUnderwatch,10)});
    auto c=underwatch.add("first contact staged",b,{sceneObserved(kUnderwatch,10)});
    auto breach=underwatch.add("wall breach",c,{sceneEvent(kUnderwatch,10,kBreachEntry),device(kUnderwatch,108,1.F),speech(5)});
    a=underwatch.add("first contact cleared",breach,{cleared(Cohort::firstContactClear)});
    underwatch.add("Cabal purpose",a,{speech(6)});
    a=underwatch.add("centurion approach",arrival,{visit(kUnderwatch,"pt_centurion_intro")});
    b=underwatch.add("centurion scene",a,{scene(kUnderwatch,18),bind(kUnderwatch,21)});
    c=underwatch.add("centurion staged",b,{sceneObserved(kUnderwatch,18)});
    auto centurion=underwatch.add("centurion entry",c,{sceneEvent(kUnderwatch,18,kEntry)});
    a=underwatch.add("centurion reinforcement area",arrival,{visit(kUnderwatch,"pt_centurion_intro_reinforce")});
    auto rush=underwatch.add("centurion reinforcements",a,{batch(SpawnCheckpoint::centurionRush)});
    a=underwatch.add("drop pod area",arrival,{visit(kUnderwatch,"pt_drop_pod")});
    auto pod=underwatch.add("drop pod reinforcements",a,{batch(SpawnCheckpoint::dropPod)});
    a=underwatch.add("civilian run",arrival,{visit(kUnderwatch,"pt_civ_run_b")});
    // Front-A civilians 45/46 also belong to Cayde's authored cast. Starting
    // their standalone run scenes57/58 here sends them through his closed
    // door before that scene starts. Leave those two to Cayde's own sequence;
    // keep the explosion/front-B evacuation and its radio cue unchanged.
    underwatch.add("evacuation line",a,{scene(kUnderwatch,56),scene(kUnderwatch,59),scene(kUnderwatch,60),scene(kUnderwatch,61),speech(7)});
    a=underwatch.add("hero moment area",arrival,{visit(kUnderwatch,"pt_hero_moment")});
    underwatch.add("hero moment",a,{scene(kUnderwatch,64)});
    auto clear=underwatch.add("approach clear",breach|centurion|rush|pod,{cleared(Cohort::breachApproach)});
    a=underwatch.add("Cayde near",clear,{reached(Milestone::caydeNear)});
    b=a;
    // Reserve Cayde, the three Legionaries and the aiming target, bind the named cells, then
    // activate with the seven authored participants and deliver the entry gate input.
    c=underwatch.add("Cayde scene",b,{scene(kUnderwatch,17),bind(kUnderwatch,29),bind(kUnderwatch,31),bind(kUnderwatch,33)});
    a=underwatch.add("Cayde staged",c,{sceneObserved(kUnderwatch,17)});
    // Reserving his source alone cannot create Shaxx: the native Scene owns
    // acquisition. Start its quiet setup with Cayde, but withhold its entry
    // input (and therefore speech/door performance) until the approach.
    b=underwatch.add("Cayde entry",a,{sceneEvent(kUnderwatch,17,kEntry),object(kUnderwatch,81,false),batch(SpawnCheckpoint::shaxx),scene(kUnderwatch,14)});
    auto shaxx=underwatch.add("Shaxx staged",b,{sceneObserved(kUnderwatch,14)});
    c=underwatch.add("Cayde handoff",b,{sceneObserved(kUnderwatch,17,SceneEvent::performanceFinished)});
    // Collision releases at scene entry, not at the end of his performance.
    // The after-Cayde score still follows his completed handoff.
    auto cayde=underwatch.add("Cayde score handoff",c,{music(ms::afterCayde)});
    a=underwatch.add("Shaxx near",cayde,{reached(Milestone::shaxxNear)});
    underwatch.add("armory",a|shaxx,{sceneEvent(kUnderwatch,14,kEntry),objective(3),finish()});

    // Armory: the authored door volume starts the armory score; the post-gun guards
    // and frames are a Scene, and the evacuation announcement plays past the weapon racks.
    auto& armory=phases[static_cast<std::size_t>(Section::armory)];armory.name("Homecoming / Armory");
    // Entry already proves PT_PLAYER_NEAR_SHAXX / the Shaxx entry input. His
    // child-26 door performance may hold forever and is NOT a gear-step gate.
    armory.add("weapon rack",0,{batch(SpawnCheckpoint::armory),device(kUnderwatch,117,1.F),object(kUnderwatch,103)});
    a=armory.add("armory door",0,{area(kUnderwatchExit,"tv_music_shaxx_door")});
    armory.add("armory music",a,{music(ms::shaxxDoor)});
    a=armory.add("post gun area",0,{visit(kUnderwatch,"pt_postgun")});
    //armory.add("evacuation announcement",a,{speech(30)});
    a=armory.add("Shaxx door opening",0,{sceneObserved(kUnderwatch,14,SceneEvent::entryCue)});
    b=armory.add("post gun",a,{scene(kUnderwatch,111)});
    c=armory.add("post gun staged",b,{sceneObserved(kUnderwatch,111)});
    // Parameter 0 is the friendly frame; parameter 1 is the Legionary. The
    // first signal removes 1526DC06 but leaves the Cabal's F9521E4E protection.
    // Release stage two only after the native opening handoff, not a timer.
    auto postGunRelease=armory.add("post gun opening release",c,{sceneEvent(kUnderwatch,111,kPostGunRelease)});
    postGunRelease=armory.add("post gun opening released",postGunRelease,{sceneObserved(kUnderwatch,111,SceneEvent::combatOpeningReleased),sceneObserved(kUnderwatch,111,SceneEvent::combatHeld)});
    postGunRelease=armory.add("post gun combat release",postGunRelease,{sceneEvent(kUnderwatch,111,kCoverCombatRelease)});
    postGunRelease=armory.add("post gun damage released",postGunRelease,{sceneObserved(kUnderwatch,111,SceneEvent::combatDamageReleased)});
    a=armory.add("weapon taken",0,{reached(Milestone::weaponGranted)});
    b=armory.add("find Zavala",a,{object(kUnderwatch,103,false),speech(28),objective(2)});
    c=armory.add("military entrance",b,{visit(kUnderwatchExit,"pt_goto_military")});
    armory.add("evacuation announcement", c, { speech(30) });
    armory.add("military",c|postGunRelease,{finish()});

    // Military: the overlook squads, the early friendlies, the fake fight and the corridor
    // squads are placed on entry so nothing drops in front of the player; the hallway pod
    // is the authored drop-pod arrival. Both Amanda gates are held closed until their
    // areas are cleared and every open waits for the applied device position.
    auto& military=phases[static_cast<std::size_t>(Section::military)];military.name("Homecoming / Military");
    arrival=military.add("hangar route",0,{objective(4),device(kMilitary,40,0.F),device(kMilitary,41,0.F),batch(SpawnCheckpoint::hangar)});
    const auto fakeFightStart=military.add("fake fight",0,{scene(kMilitary,70),scene(kMilitary,72),scene(kMilitary,74),scene(kMilitary,77)});
    std::uint32_t fakeFight{};
    // A slow acquisition in one encounter must not keep the other Cabal held.
    constexpr std::uint16_t hallwayScenes[]{70,72,74,77};
    constexpr std::string_view holdNames[]{"cover A held","cover B held","cover C held","corridor held"};
    constexpr std::string_view releaseNames[]{"cover A combat","cover B combat","cover C combat","corridor combat"};
    for(std::size_t i=0;i<std::size(hallwayScenes);++i) {
        const auto held=military.add(holdNames[i],fakeFightStart,{sceneObserved(kMilitary,hallwayScenes[i],SceneEvent::combatHeld)});
        fakeFight|=military.add(releaseNames[i],held,{sceneEvent(kMilitary,hallwayScenes[i],kCoverCombatRelease)});
    }
    a=military.add("hangar spawn area",arrival,{visit(kMilitary,"pt_hangar_spawn")});
    b=military.add("drop pod",a,{object(kMilitary,2)});
    c=military.add("drop pod present",b,{present(kMilitary,2)});
    a=military.add("hallway destruction",c,{batch(SpawnCheckpoint::hallwayPod),music(ms::firstPod)});
    b=military.add("first gate cleared",a,{cleared(Cohort::gateStartClear)});
    c=military.add("first gate opens",b,{device(kMilitary,40,1.F)});
    a=military.add("first gate open",c,{posed(kMilitary,40,1.F)});
    c=military.add("second gate cleared",a,{cleared(Cohort::gateHangarClear)});
    a=military.add("second gate opens",c,{device(kMilitary,41,1.F)});
    const auto hangarCleared=military.add("second gate open",a,{posed(kMilitary,41,1.F)});b=hangarCleared;
    c=military.add("destroyer",b,{object(kMilitary,0)});
    a=military.add("destroyer present",c,{present(kMilitary,0)});
    const auto shipVisible=a;
    military.add("destroyer missiles",a,{object(kMilitary,48),object(kMilitary,49),object(kMilitary,50),object(kMilitary,51),object(kMilitary,52),object(kMilitary,53)});
    a=military.add("hangar window",arrival,{visit(kMilitaryRoute,"pt_dialogue_hangar_window")});
    // The window can be crossed before the ship is created. Queue its sighting
    // only after both the sightline and the ship's native presence are known.
    military.add("command ship sighting",a|shipVisible,{speech(34)});
    // The reveal owns music, not the later corridor conversation.
    a=military.add("hangar reveal",arrival,{area(kMilitaryRoute,"tv_music_cabal_ship_reveal")});
    military.add("hangar reveal score",a,{music(ms::cabalShipReveal)});
    a=military.add("hangar combat area",arrival,{visit(kMilitary,"pt_hangar_combat")});
    military.add("hangar floor",a,{speech(35)});
    a=military.add("plaza route",arrival|hangarCleared,{visit(kMilitaryRoute,"pt_goto_plaza")});
    military.add("to the plaza", a | fakeFight, { music(ms::goingToPlaza), speech(37),finish() });

    // First assault -> first Ward -> second assault -> second Ward -> last
    // assault. Enemy clearance, not a delayed radio request, ends each wave.
    auto& plaza=phases[static_cast<std::size_t>(Section::plaza)];plaza.name("Homecoming / Plaza");
    arrival=plaza.add("plaza arrival",0,{objective(5),object(kPlaza,18,false),object(kPlaza,44,false),object(kPlaza,24,false),
        object(kPlaza,25,false),object(kPlaza,46,false),object(kPlaza,47,false)});
    plaza.add("first carriers", arrival, { object(kPlazaProps,16),object(kPlazaProps,17),device(kPlazaProps,19,1.F),device(kPlazaProps,20,1.F) });
    // there should be more carriers here!! It's very likely that they're in other sections of the assault.
    plaza.add("skybox ships A",arrival,{object(kPlazaProps,0),object(kPlazaProps,4),object(kPlazaProps,9),object(kPlazaProps,10),object(kPlazaProps,11),object(kPlazaProps,12)});
    plaza.add("skybox ships B",arrival,{object(kPlazaProps,13),object(kPlazaProps,14),object(kPlazaProps,15),object(kPlazaProps,6),object(kPlazaProps,7),object(kPlazaProps,8)});
    a=plaza.add("Zavala arrival scene",arrival,{batch(SpawnCheckpoint::plaza),scene(kPlaza,2),bind(kPlaza,7)});
    b=plaza.add("Zavala arrival staged",a,{sceneObserved(kPlaza,2)});
    auto zavala=plaza.add("Zavala arrival entry",b,{sceneEvent(kPlaza,2,kZavalaArrive),sceneEvent(kPlaza,2,kZavalaArriveB)});
    a=plaza.add("Traveler sighting area",arrival,{area(kPlazaRoute,"tv_dialog_military_11")});
    // The preceding room's Ghost line owns the plaza Cabal ship's entrance.
    // A queued line may wait behind radio; use its accepted dispatch, not
    // section arrival, objective changes or the end of the spoken window.
    b=plaza.add("Traveler sighting",a,{speech(40),speaking(40),music(ms::plazaCabalShip) });
    plaza.add("plaza ship entrance",a,{object(kPlazaProps,5)});
    a=plaza.add("meet Zavala area",arrival,{visit(kPlaza,"pt_plaza_zavala_meet")});
    b=plaza.add("meet Zavala",a,{object(kPlazaProps,0,false),object(kPlaza,8),speech(50)});
    auto arrivalFinished=plaza.add("Zavala arrival finished",zavala,{sceneObserved(kPlaza,2,SceneEvent::performanceFinished)});
    c=plaza.add("Zavala combat",b|arrivalFinished,{mechanic(asset(kPlaza,43,2),Mechanic::stopScene),scene(kPlaza,3)});
    a=plaza.add("opening Cabal cleared",c,{cleared(Cohort::plazaInit)});
    b=a;
    auto first=plaza.add("first assault",b,{objective(7),speech(52),object(kPlaza,45),batch(SpawnCheckpoint::assault1)});
    a=plaza.add("first assault cleared",first,{cleared(Cohort::wave1),cleared(Cohort::interim)});
    b=plaza.add("first assault repelled",a,{control(Mechanic::assaultRepelled)});
    c=plaza.add("first barrage",b,{speech(53),mechanic(asset(kPlaza,43,3),Mechanic::stopScene),scene(kPlaza,4),object(kPlaza,44,false),object(kPlaza,46,false),object(kPlaza,47,false),mechanic(asset(kPlaza,23,48),Mechanic::powerOff)});
    a=plaza.add("Ward staged",c,{sceneObserved(kPlaza,4)});
    // The native performer 80B3A858 casts its own shield (80B826F4) at
    // authored time 5.3, alongside Zavala's hands-out beat. Object18 is a
    // second, standalone shield (80B826F1); enabling it at entry duplicates
    // the purple field before the actor animation reaches that beat.
    b=plaza.add("Ward of Dawn",a,{sceneEvent(kPlaza,4,kZavalaWard)});
    c=plaza.add("barrage held",b,{sceneObserved(kPlaza,4,SceneEvent::performanceFinished)});
    plaza.add("second assault approaches",c,{object(kPlaza,18,false),mechanic(asset(kPlaza,43,4),Mechanic::stopScene),finish()});

    auto& waves=phases[static_cast<std::size_t>(Section::plazaWaves)];waves.name("Homecoming / Plaza assaults");
    arrival=waves.add("second assault",0,{scene(kPlaza,3),speech(51),batch(SpawnCheckpoint::assault2)});
    a=waves.add("second wave cleared",arrival,{cleared(Cohort::wave2)});
    b=waves.add("we hold here",a,{control(Mechanic::assaultRepelled),speech(55)});
    b=waves.add("second barrage warning",b,{spoken(55)});
    c=waves.add("second barrage",b,{speech(57),mechanic(asset(kPlaza,43,3),Mechanic::stopScene),scene(kPlaza,4)});
    a=waves.add("second Ward staged",c,{sceneObserved(kPlaza,4)});
    b=waves.add("second Ward of Dawn",a,{sceneEvent(kPlaza,4,kZavalaWard)});
    c=waves.add("second barrage held",b,{sceneObserved(kPlaza,4,SceneEvent::performanceFinished)});
    a=waves.add("barrage strike",c,{object(kPlaza,18,false),object(kPlaza,44),mechanic(asset(kPlaza,43,4),Mechanic::stopScene),scene(kPlaza,5)});
    b=waves.add("death staged",a,{sceneObserved(kPlaza,5)});
    c=waves.add("death entry",b,{sceneEvent(kPlaza,5,kZavalaDeath)});
    a=waves.add("Zavala struck",c,{sceneObserved(kPlaza,5,SceneEvent::completed)});
    b=waves.add("Zavala down",a,{speech(47),mechanic(asset(kPlaza,43,5),Mechanic::stopScene),mechanic(asset(kPlaza,2,7),Mechanic::retireMember),
        object(kPlaza,44,false),object(kPlaza,25),device(kPlaza,26,1.F),mechanic(kReviveInteract,Mechanic::arm)});
    c=waves.add("Zavala revived",b,{used(kPlaza,24)});
    a=waves.add("revival",c,{mechanic(kReviveInteract,Mechanic::disarm),device(kPlaza,26,0.F),object(kPlaza,25,false),scene(kPlaza,2),bind(kPlaza,7)});
    b=waves.add("revival staged",a,{sceneObserved(kPlaza,2)});
    // Only the Zavala branch is a revival. The second input replays the four
    // scripted Cabal kills and must not fire a second time.
    c=waves.add("revival entry",b,{sceneEvent(kPlaza,2,kZavalaArrive)});
    a=waves.add("revival complete",c,{sceneObserved(kPlaza,2,SceneEvent::performanceFinished)});
    a=waves.add("Zavala combat resumes",a,{mechanic(asset(kPlaza,43,2),Mechanic::stopScene),scene(kPlaza,3)});
    b=waves.add("last assault",a,{speech(54),batch(SpawnCheckpoint::assault3),object(kPlazaProps,18),object(kPlazaProps,22),object(kPlazaProps,23),object(kPlazaProps,24)});
    c=waves.add("last carriers",a,{device(kPlazaProps,21,1.F),device(kPlazaProps,31,1.F),device(kPlazaProps,32,1.F),device(kPlazaProps,33,1.F)});

    b=waves.add("last assault cleared",b|c,{cleared(Cohort::wave3)});
    c=waves.add("plaza held",b,{control(Mechanic::assaultRepelled)});
    a=waves.add("shuttles away",c,{mechanic(asset(kPlaza,43,3),Mechanic::stopScene),object(kPlaza,45,false),speech(59),objective(9)});
    b=waves.add("plaza exit",a,{visit(kPlazaRoute,"pt_goto_boulevard")});
    waves.add("boulevard",b,{music(ms::leavingPlaza),finish()});

    // Boulevard: Ikora's scene is prepared on arrival; her entry input follows the authored
    // start trigger, and the Nova blast is released after the recovered entry window. The
    // bazaar squads are placed when the bazaar comes into view so they wait in cover.
    auto& boulevard=phases[static_cast<std::size_t>(Section::boulevard)];boulevard.name("Homecoming / Boulevard");
    arrival=boulevard.add("boulevard arrival",0,{objective(8),device(kBoulevard,0,1.F),batch(SpawnCheckpoint::boulevard),scene(kBoulevard,16)});
    a=boulevard.add("Ikora staged",arrival,{sceneObserved(kBoulevard,16),visit(kBoulevard,"pt_start_cabal_movement")});
    auto ikora=boulevard.add("Cabal movement",a,{sceneEvent(kBoulevard,16,kIkoraMove)});
    a=boulevard.add("Ikora start area",arrival,{visit(kBoulevard,"pt_start_ikora")});
    b=boulevard.add("Ikora entry",a|ikora,{sceneEvent(kBoulevard,16,kEntry), music(ms::ikoraAppearance)});
    c=boulevard.add("entry cue",b,{sceneObserved(kBoulevard,16,SceneEvent::entryCue)});
    auto blast=boulevard.add("Nova blast",c,{object(kBoulevard,23),sceneEvent(kBoulevard,16,kIkoraBlast)});
    a=boulevard.add("Speaker report",blast,{sceneObserved(kBoulevard,16,SceneEvent::performanceFinished)});
    // 80B3A848's timed speech track already owns row 72. Its completion
    // includes the pickup order; do not queue that same exchange again.
    boulevard.add("Zavala sends Holliday",a,{objective(10)});
    a=boulevard.add("Speaker route",arrival,{visit(kBoulevardRoute,"pt_goto_speaker")});
    boulevard.add("Speaker close",a,{speech(64)});
    a=boulevard.add("bazaar sighting",arrival,{area(kBoulevardRoute,"tv_bazaar")});
    auto placed=boulevard.add("bazaar sighting report",a,{speech(74)});
    a=boulevard.add("bazaar entry",arrival,{visit(kBoulevard,"pt_bazaar")});
    auto entered=boulevard.add("Holliday inbound",a,{speech(75),objective(10)});
    a=boulevard.add("Ikora's Cabal blasted",blast,{cleared(Cohort::ikoraCabal)});
    b=boulevard.add("bazaar door opens",a,{device(kBoulevard,9,1.F)});
    c=boulevard.add("bazaar door open",b,{posed(kBoulevard,9,1.F)});
    boulevard.add("bazaar breach",c,{sequence(kBoulevard,33),object(kBoulevard,25,false),object(kBoulevard,27,false),control(Mechanic::releaseDoor)});
    a=boulevard.add("bazaar cleared",placed|entered,{cleared(Cohort::bazaarAll)});
    b=boulevard.add("Hawk",a,{object(kBoulevard,12)});
    c=boulevard.add("Hawk present",b,{present(kBoulevard,12)});
    a=boulevard.add("Holliday arrives",c,{speech(76)});
    b=boulevard.add("Holliday spoken",a,{spoken(76)});
    c=boulevard.add("pickup area",b,{visit(kBoulevardRoute,"pt_goto_sky_battle")});
    boulevard.add("pickup",c,{control(Mechanic::pickup),finish()});

    // Command ship: the deck score starts on landing and the console room is
    // populated before the scan. Dialogue follows console and stairwell events.
    // Console doors follow the matching scan generation.
    auto& ship=phases[static_cast<std::size_t>(Section::ship)];ship.name("Homecoming / Command ship");
    arrival=ship.add("ship arrival",0,{objective(13),music(ms::battleshipDeck),speech(77),batch(SpawnCheckpoint::shipInterior),ghost(kShip,166,true)});
    a=ship.add("console approach",arrival,{visit(kShip,"pt_pods_door_a")});
    ship.add("shield hologram",a,{speech(78)});
    a=ship.add("console scanned",arrival,{reached(Milestone::consoleScanned)});
    ship.add("console doors",a,{ghost(kShip,166,false),device(kShip,167,1.F),device(kShip,56,1.F),device(kShip,57,1.F),objective(14),speech(79)});
    a=ship.add("Hawk area",arrival,{visit(kShip,"pt_hawk")});
    ship.add("Hawk flyby",a,{object(kShip,165),device(kShip,164,1.F)});
    a=ship.add("pod launch area",arrival,{visit(kShip,"pt_drop_pod_launch")});
    ship.add("pod launch",a,{object(kShip,150),device(kShip,151,1.F)});
    a=ship.add("pod door A",arrival,{visit(kShip,"pt_pods_door_a")});
    ship.add("start impacts",a,{object(kShip,78),object(kShip,80),object(kShip,82),object(kShip,84),object(kShip,86),object(kShip,88)});
    ship.add("start impact devices",a,{device(kShip,77,.5F),device(kShip,79,.5F),device(kShip,81,.5F),device(kShip,83,.5F),device(kShip,85,.5F),device(kShip,87,.5F)});
    a=ship.add("damaged hall",arrival,{visit(kShip,"pt_damaged_hall")});
    ship.add("hall impacts",a,{object(kShip,90),object(kShip,92),object(kShip,94),object(kShip,96),object(kShip,98),object(kShip,100)});
    ship.add("hall impact devices",a,{device(kShip,89,.5F),device(kShip,91,.5F),device(kShip,93,.5F),device(kShip,95,.5F),device(kShip,97,.5F),device(kShip,99,.5F)});
    a=ship.add("stairs",arrival,{visit(kShip,"pt_damaged_hall_stairs")});
    ship.add("stair impacts",a,{object(kShip,102),object(kShip,104),object(kShip,106),object(kShip,108),object(kShip,110),batch(SpawnCheckpoint::shipDeck),speech(82)});
    ship.add("stair impact devices",a,{device(kShip,101,.5F),device(kShip,103,.5F),device(kShip,105,.5F),device(kShip,107,.5F),device(kShip,109,.5F)});
    a=ship.add("stairs door area",arrival,{visit(kShip,"pt_damaged_hall_stairs_door")});
    ship.add("stairs door",a,{device(kShip,58,1.F)});
    a=ship.add("deck start",arrival,{visit(kShip,"pt_deck_start")});
    ship.add("deck front props",a,{object(kShip,112),object(kShip,114),object(kShip,116),object(kShip,118),object(kShip,120),object(kShip,122),object(kShip,124)});
    ship.add("deck impacts",a,{object(kShip,126),object(kShip,128),object(kShip,130),device(kShip,111,.5F),device(kShip,113,.5F),device(kShip,115,.5F),device(kShip,117,.5F),device(kShip,119,.5F)});
    ship.add("deck impact devices",a,{device(kShip,121,.5F),device(kShip,123,.5F),device(kShip,125,.5F),device(kShip,127,.5F),device(kShip,129,.5F)});
    a=ship.add("deck boss area",arrival,{visit(kShip,"pt_deck_boss")});
    ship.add("deck boss",a,{finish()});

    // Shield generator: the interior squads are placed behind their doors on entry; the
    // sabotage score starts in the generator chamber and the turbine lines follow
    // the first and second destruction receipts.
    auto& generator=phases[static_cast<std::size_t>(Section::generator)];generator.name("Homecoming / Shield generator");
    arrival=generator.add("boss announce",0,{batch(SpawnCheckpoint::generator),sequence(kShip,174)});
    auto approach=generator.add("engine room lower",arrival,{visit(kShip,"pt_engine_room_lower")});
    generator.add("almost there",approach,{speech(84)});
    // Load the beam, moving generator assembly and lights from the preceding
    // room. The chamber objective and damageable turbine setup remain separate.
    generator.add("reactor visuals preload",approach,{object(kShip,148),object(kShip,149),device(kShip,61,.1F),device(kShip,62,.1F),device(kShip,64,.1F),device(kShip,63,1.F)});
    generator.add("reactor lights preload",approach,{device(kShip,159,1.F),device(kShip,160,1.F),device(kShip,161,1.F),device(kShip,162,1.F),device(kShip,65,1.F),device(kShip,66,1.F),device(kShip,67,1.F)});
    // This door is an approach trigger, not an encounter-clear reward. Deck
    // bosses remain active combatants but cannot block reactor access.
    b=generator.add("generator entrance opens",approach,{device(kShip,59,1.F),speech(85)});
    c=generator.add("generator entrance open",b,{posed(kShip,59,1.F)});
    auto chamber=generator.add("generator chamber",c,{visit(kShipRoute,"pt_destroy_battleship")});
    generator.add("overload",chamber,{music(ms::battleshipSabotage),objective(11),speech(86),object(kShip,142),object(kShip,144),object(kShip,146)});
    auto turbines=generator.add("turbines",chamber,{device(kShip,143,.1F),device(kShip,145,.1F),device(kShip,147,.1F)});
    a=generator.add("turbine A destroyed",turbines,{reached(Milestone::generatorA)});
    auto killA=generator.add("turbine A dark",a,{device(kShip,160,0.F)});
    a=generator.add("turbine B destroyed",turbines,{reached(Milestone::generatorB)});
    auto killB=generator.add("turbine B dark",a,{device(kShip,161,0.F)});
    a=generator.add("turbine C destroyed",turbines,{reached(Milestone::generatorC)});
    auto killC=generator.add("turbine C dark",a,{device(kShip,162,0.F)});
    a=generator.add("first turbine destroyed",turbines,{reached(Milestone::turbineFirst)});
    generator.add("first turbine report",a,{speech(88)});
    a=generator.add("second turbine destroyed",turbines,{reached(Milestone::turbineSecond)});
    generator.add("second turbine report",a,{speech(90)});
    a=generator.add("shutdown",killA|killB|killC,{device(kShip,61,.2F),device(kShip,62,.2F),device(kShip,64,.2F),device(kShip,63,0.F),device(kShip,159,0.F),object(kShip,68),object(kShip,69),object(kShip,70)});
    b=generator.add("escape opens",a,{music(ms::run),object(kShip,76),device(kShip,163,1.F),device(kShip,60,1.F),objective(12),speech(91),speech(92,5000),batch(SpawnCheckpoint::escape)});
    c=generator.add("escape open",b,{posed(kShip,60,1.F)});
    generator.add("escape",c,{finish()});

    // Escape: the four authored explosion scenes and the finale own their timing; leaving
    // the terminal volume ends the escape (v34) and hands the ending to the native outro.
    auto& escape=phases[static_cast<std::size_t>(Section::escape)];escape.name("Homecoming / Escape");
    a=escape.add("explosion A area",0,{visit(kShip,"pt_escape_explosion_a")});
    escape.add("explosion A",a,{scene(kShip,137)});
    a=escape.add("explosion B area",0,{visit(kShip,"pt_escape_explosion_b")});
    escape.add("explosion B",a,{scene(kShip,138)});
    a=escape.add("explosion C area",0,{visit(kShip,"pt_escape_explosion_c")});
    escape.add("explosion C",a,{scene(kShip,139)});
    a=escape.add("explosion D area",0,{visit(kShip,"pt_escape_explosion_d")});
    escape.add("explosion D",a,{scene(kShip,140)});
    a=escape.add("hangar reached",0,{visit(kShipRoute,"pt_goto_end")});
    b=escape.add("finale",a,{scene(kShip,141),music(ms::ending)});
    c=escape.add("finale plays",b,{sceneObserved(kShip,141)});
    escape.add("departure",c,{mechanic(asset(kShip,43,137),Mechanic::stopScene),mechanic(asset(kShip,43,138),Mechanic::stopScene),
        mechanic(asset(kShip,43,139),Mechanic::stopScene),mechanic(asset(kShip,43,140),Mechanic::stopScene),mechanic(asset(kShip,43,141),Mechanic::stopScene),
        {Operation::complete,kModule,6,Wait::requested}});
}
bool Mission::valid() const noexcept {
    std::array<unsigned,std::size(kSpawnBatches)> checkpoints{};
    const auto valid=[&](const Graph& graph) {
        if(!coo::Executor::valid(graph.definition)) {return false;}
        for(const auto& step:graph.definition.steps) for(const auto& command:step.commands) {
            if(!command.asset.registry || (!command.asset.definition && command.asset!=kModule)) {return false;}
            if(command.operation==coo::Operation::dialogue) {
                const auto row=dialogue_row(command.argument);
                if(row>=std::size(kDialogue) || !kDialogue[row].durationMs || kDialogue[row].sceneOwned) {return false;}
            }
            if(command.operation==coo::Operation::observation && command.asset==kDialogueAsset) {
                const auto row=command.argument&~kDialogueStarted;
                if(row>=std::size(kDialogue) || !kDialogue[row].durationMs) {return false;}
            }
            if(command.operation==coo::Operation::mechanic && command.asset==kMusicAsset
                && ((command.argument&0xFFU)!=static_cast<std::uint32_t>(Mechanic::music)
                    || ((command.argument>>8)!=music_section::none && (command.argument>>8)>=music_section::count))) {return false;}
            if(command.operation==coo::Operation::population) {
                if(command.asset!=kModule || command.argument>=std::size(kSpawnBatches)) {return false;}
                const auto& batch=kSpawnBatches[command.argument];
                if(static_cast<unsigned>(batch.id)!=command.argument
                    || static_cast<std::size_t>(batch.section)!=static_cast<std::size_t>(&graph-phases.data())
                    || (batch.cohorts.empty() && batch.casts.empty()) || ++checkpoints[command.argument]!=1) {return false;}
                // Loose cohorts must not acquire Scene-owned sources. Cast staging
                // uses the exact authored scene's source list instead.
                for(const auto cohort:batch.cohorts) {
                    const auto id=static_cast<std::size_t>(cohort);if(id>=std::size(kCohorts)) {return false;}
                    for(const auto member:kCohorts[id].members) {
                        bool loose=false;
                        for(const auto& spawn:kSpawns) {if(spawn.registry==member.registry && spawn.source==member.slot) {loose=!spawn.sceneOwned;}}
                        if(!loose) {return false;}
                    }
                }
                for(const auto cast:batch.casts) {
                    bool found=false;
                    for(const auto& scene:kScenes) if(scene.asset==cast) {
                        found=true;
                        for(const auto& member:scene.cast) if(member.type==1) {
                            bool source=false;
                            for(const auto& spawn:kSpawns) {source|=spawn.registry==member.registry && spawn.source==member.slot;}
                            if(!source) {return false;}
                        }
                    }
                    if(!found) {return false;}
                }
            }
            if(command.operation==coo::Operation::scene) {
                bool found=false;
                for(const auto& scene:kScenes) {
                    if(scene.asset!=command.asset) {continue;}found=true;
                    if(command.argument) {
                        bool input=false;for(const auto key:scene.inputs) {input|=key==command.argument;}
                        if(!input) {return false;}
                    }
                }
                if(!found) {return false;}
            }
        }
        return true;
    };
    return std::all_of(phases.begin(),phases.end(),valid)
        && std::all_of(checkpoints.begin(),checkpoints.end(),[](unsigned n) {return n==1;});
}
const Mission& mission() noexcept {static const Mission value;return value;}
}
