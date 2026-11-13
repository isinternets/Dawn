#include "mission.h"
#include <bit>
#include <algorithm>
namespace dawn::state::activity::vanilla::one_au {
namespace {
using coo::CommandSpec;using coo::Operation;using coo::Wait;
CommandSpec objective(std::size_t i) {return {Operation::objective,kDirectiveAsset,kObjectives[i],Wait::requested};}
CommandSpec speech(std::uint8_t row) {return {Operation::dialogue,kDialogueAsset,row,Wait::requested};}
CommandSpec spoken(std::uint8_t row) {return {Operation::observation,kDialogueAsset,row,Wait::observed};}
CommandSpec visit(std::uint32_t r,std::uint16_t slot) {return {Operation::observation,volume(r,slot),0,Wait::observed};}
CommandSpec reached(Milestone m) {return {Operation::observation,kModule,static_cast<std::uint32_t>(m),Wait::observed};}
CommandSpec device(std::uint32_t r,std::uint16_t slot,float position) {return {Operation::device,asset(r,23,slot),std::bit_cast<std::uint32_t>(position),Wait::requested};}
CommandSpec posed(std::uint16_t slot,float position) {return {Operation::observation,asset(kProcessing,23,slot),std::bit_cast<std::uint32_t>(position),Wait::observed};}
CommandSpec object(std::uint32_t r,std::uint16_t slot,bool active=true) {return {Operation::device,asset(r,4,slot),active?1U:0U,Wait::requested};}
CommandSpec sequence(std::uint32_t r,std::uint16_t slot) {return {Operation::device,asset(r,5,slot),1,Wait::requested};}
CommandSpec scene(std::uint32_t r,std::uint16_t slot) {return {Operation::scene,asset(r,43,slot),0,Wait::requested};}
CommandSpec control(Mechanic value) {return {Operation::mechanic,kModule,static_cast<std::uint32_t>(value),Wait::requested};}
CommandSpec arm(Interaction i) {return {Operation::mechanic,kInteractionAssets[static_cast<std::size_t>(i)],static_cast<std::uint32_t>(Mechanic::arm),Wait::requested};}
CommandSpec event(Interaction i,Event e) {return {Operation::observation,kInteractionAssets[static_cast<std::size_t>(i)],static_cast<std::uint32_t>(e),Wait::observed};}
CommandSpec spawn(Cohort c) {return {Operation::population,kModule,static_cast<std::uint32_t>(c),Wait::requested};}
CommandSpec cleared(Cohort c) {return {Operation::observation,kModule,0x100U+static_cast<std::uint32_t>(c),Wait::observed};}
CommandSpec destroyed(std::uint32_t count) {return {Operation::observation,kModule,0x200U+count,Wait::observed};}
// These hatch lanes rest open: 0 is open and 1 is shut.
inline constexpr float kHatchOpen=0.F,kHatchShut=1.F;
// The first refinery lid is fully open. The other three keep a 40% clearance
// gap until opening fully for their defense waves (open=0, shut=1).
inline constexpr float kRefineryHatchRest=.6F;
void processing_props(Graph& g,std::uint32_t dependency) {
    g.add("refinery hatch lids",dependency,{object(kProcessing,17),object(kProcessing,18),object(kProcessing,19),object(kProcessing,20),object(kProcessing,16),object(kProcessing,30),device(kProcessing,7,1.F)});
    g.add("refinery debris A-E",dependency,{object(kProcessing,21),object(kProcessing,22),object(kProcessing,23),object(kProcessing,24),object(kProcessing,25)});
    g.add("refinery debris F-I",dependency,{object(kProcessing,26),object(kProcessing,27),object(kProcessing,28),object(kProcessing,29)});
    g.add("refinery initial poses",dependency,{device(kProcessing,0,kHatchOpen),device(kProcessing,1,kRefineryHatchRest),device(kProcessing,2,kRefineryHatchRest),device(kProcessing,3,kRefineryHatchRest),device(kProcessing,5,0.F),device(kProcessing,6,0.F),device(kProcessing,8,0.F),device(kProcessing,13,0.F)});
}
CommandSpec finish() {return control(Mechanic::finishSection);}
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
Mission::Mission() noexcept {
    for(auto& phase:phases) {phase.storage_=&storage;}
    bridgeCheckpoint.storage_=processingCheckpoint.storage_=&storage;
    auto& landing=phases[0];landing.name("1AU / Starboard Landing");
    auto a=landing.add("arrival",0,{objective(0),speech(0),spawn(Cohort::catwalk),spawn(Cohort::mercury)});
    landing.add("retracted bridge",a,{device(kBridge,0,1.F),device(kBridge,1,1.F),device(kBridge,2,1.F),device(kBridge,3,1.F),device(kBridge,4,1.F),device(kBridge,5,1.F)});
    auto b=landing.add("leave landing",a,{visit(kLanding,85)});
    landing.add("catwalk reinforcement",b,{spawn(Cohort::catwalkMid)});
    auto c=landing.add("pipe approach",b,{visit(kBridge,203)});
    landing.add("pipe crossing",c,{spawn(Cohort::pipes),speech(2)});
    auto d=landing.add("bridge approach",b,{visit(kBridge,207)});
    landing.add("enter bridge encounter",d,{finish()});

    auto& bridge=phases[1];bridge.name("1AU / Bridge console");
    a=bridge.add("secure console",0,{objective(1),spawn(Cohort::mercury)});
    b=bridge.add("console defenders cleared",a,{cleared(Cohort::mercury)});
    c=bridge.add("deploy Ghost",b,{objective(2),speech(3),arm(Interaction::bridge)});
    d=bridge.add("native console interaction",c,{event(Interaction::bridge,Event::completed)});
    auto e=bridge.add("extend bridge",d,{device(kBridge,0,0.F),device(kBridge,1,0.F),device(kBridge,2,0.F),device(kBridge,3,0.F),device(kBridge,4,0.F),device(kBridge,5,0.F)});
    auto f=bridge.add("cross bridge",e,{control(Mechanic::restrict),objective(3),speech(8),spawn(Cohort::bridge)});
    // Crossing the gap or reaching processing is valid without fighting for the console.
    auto g=bridge.add("far landing",a,{visit(kBridge,202)});
    bridge.add("far landing combat",g,{spawn(Cohort::sunLanding),spawn(Cohort::helipad)});
    auto h=bridge.add("mineral processing entrance",a,{visit(0x7674801CU,4)});
    bridge.add("follow fuel",h,{objective(4),finish()});

    // The first darkness checkpoint is committed after the Ghost scan. A wipe
    // restores the crossing, including new cargo, without replaying the console.
    bridgeCheckpoint.name("1AU / Bridge crossing checkpoint");
    a=bridgeCheckpoint.add("restore extended bridge",0,{device(kBridge,0,0.F),device(kBridge,1,0.F),device(kBridge,2,0.F),device(kBridge,3,0.F),device(kBridge,4,0.F),device(kBridge,5,0.F)});
    b=bridgeCheckpoint.add("cross bridge",a,{control(Mechanic::restrict),objective(3),spawn(Cohort::bridge)});
    c=bridgeCheckpoint.add("far landing",b,{visit(kBridge,202)});
    bridgeCheckpoint.add("far landing combat",c,{spawn(Cohort::sunLanding),spawn(Cohort::helipad)});
    d=bridgeCheckpoint.add("mineral processing entrance",c,{visit(0x7674801CU,4)});
    bridgeCheckpoint.add("follow fuel",d,{objective(4),finish()});

    auto& processing=phases[2];processing.name("1AU / Mineral Processing");
    a=processing.add("enter processing",0,{control(Mechanic::allow),objective(5),speech(9),spawn(Cohort::processingEntry),object(kProcessing,15),object(kProcessing,16),device(kProcessing,10,1.F),device(kProcessing,4,1.F)});
    processing_props(processing,a);
    b=processing.add("discover refinery blockage",a,{reached(Milestone::processingDiscovery)});
    c=processing.add("fusion cell and receptacle available",b,{objective(6),speech(10),arm(Interaction::processingCell),arm(Interaction::processingSink)});
    // Console dialogue/presentation is optional and must not gate either object
    // or overwrite the objective after the player has picked up the cell.
    d=processing.add("Mercury control",a,{visit(0xE3EC5485U,31)});
    processing.add("Mercury hologram",d,{speech(11),device(kProcessing,8,1.F),device(kProcessing,12,1.F)});
    auto i=processing.add("cell held",c,{event(Interaction::processingCell,Event::pickedUp)});
    auto j=processing.add("carry to grinder receptacle",i,{objective(7),speech(13),speech(14)});
    auto k=processing.add("cell consumed by native sink",j,{event(Interaction::processingSink,Event::inserted)});
    processing.add("grinder encounter",k,{finish()});

    processingCheckpoint.name("1AU / Processing checkpoint");
    a=processingCheckpoint.add("restore fusion cell",0,{control(Mechanic::allow),objective(6),arm(Interaction::processingCell),arm(Interaction::processingSink),object(kProcessing,15),device(kProcessing,4,1.F)});
    processing_props(processingCheckpoint,a);
    b=processingCheckpoint.add("cell held",a,{event(Interaction::processingCell,Event::pickedUp)});
    c=processingCheckpoint.add("receptacle",b,{objective(7)});
    d=processingCheckpoint.add("native insertion",c,{event(Interaction::processingSink,Event::inserted)});
    processingCheckpoint.add("grinder",d,{finish()});

    auto& grinder=phases[3];grinder.name("1AU / Ore grinder");
    a=grinder.add("seal ore tunnel",0,{device(kProcessing,4,0.F),device(kProcessing,8,1.F),device(kProcessing,13,1.F),sequence(kProcessing,35)});
    b=grinder.add("tunnel fully closed",a,{posed(4,0.F)});
    // Prepare the entrance while the tunnel seals; both must finish before combat.
    c=grinder.add("defender access hatches",a,{device(kProcessing,0,kHatchOpen),device(kProcessing,1,kHatchOpen),device(kProcessing,2,kRefineryHatchRest),device(kProcessing,3,kRefineryHatchRest)});
    d=grinder.add("first hatch route open",c,{posed(1,kHatchOpen)});
    a=grinder.add("grinder running",b|d,{objective(8),control(Mechanic::restrict),spawn(Cohort::grinderDefense)});
    b=grinder.add("defenders cleared",a,{cleared(Cohort::grinderDefense)});
    // Prepare both far entrances during wave one, so wave two can follow its clear.
    // The native spawn rule can choose either entrance; wait for both if needed.
    c=grinder.add("open far hatches",a,{device(kProcessing,2,kHatchOpen),device(kProcessing,3,kHatchOpen)});
    d=grinder.add("far hatch routes open",c,{posed(2,kHatchOpen),posed(3,kHatchOpen)});
    c=grinder.add("second hatch wave",b|d,{spawn(Cohort::grinderReinforcements)});
    d=grinder.add("reinforcements cleared",c,{cleared(Cohort::grinderReinforcements)});
    c=grinder.add("Purifier Vurst",d,{spawn(Cohort::grinderBoss)});
    d=grinder.add("Purifier defeated",c,{cleared(Cohort::grinderBoss)});
    e=grinder.add("ore passage available",d,{device(kProcessing,4,1.F),device(kProcessing,5,1.F),device(kProcessing,6,1.F),object(kProcessing,15,false),speech(17),objective(9),control(Mechanic::allow)});
    const auto debrisA=grinder.add("purge chamber debris A-E",e,{object(kProcessing,21,false),object(kProcessing,22,false),object(kProcessing,23,false),object(kProcessing,24,false),object(kProcessing,25,false)});
    const auto debrisB=grinder.add("purge chamber debris F-I",e,{object(kProcessing,26,false),object(kProcessing,27,false),object(kProcessing,28,false),object(kProcessing,29,false),device(kProcessing,13,0.F)});
    f=grinder.add("enter ore tunnel",e|debrisA|debrisB,{visit(0x1405BF98U,7)});
    grinder.add("leave processing",f,{finish()});

    auto& tunnel=phases[4];tunnel.name("1AU / Ore tunnel");
    a=tunnel.add("processed Mercury",0,{speech(18),spawn(Cohort::readyOne),device(kTunnel,0,1.F),sequence(kTunnel,2),device(kTunnel,1,0.F)});
    b=tunnel.add("ready room reinforcements",a,{reached(Milestone::readyOneReinforce)});
    c=tunnel.add("ready room second wave",b,{spawn(Cohort::readyOneReinforce)});
    d=tunnel.add("ready room clear",c,{cleared(Cohort::readyOne),cleared(Cohort::readyOneReinforce)});
    tunnel.add("tumbler defenders",d,{device(kTunnel,1,1.F),spawn(Cohort::tumbler)});
    b=tunnel.add("tunnel exit",a,{visit(kTunnel,57)});
    c=tunnel.add("Vanguard channel",b,{speech(19),speech(20),objective(10),spawn(Cohort::tumbler)});
    d=tunnel.add("outside warning",c,{visit(0xAEA6C56DU,22)});
    tunnel.add("only way out",d,{speech(23)});
    e=tunnel.add("outside route",c,{visit(kTunnel,58)});
    tunnel.add("go outside",e,{objective(11),finish()});

    auto& sun=phases[5];sun.name("1AU / Sunside");
    // The archive snaps the foundry transport shut on entering this region, long
    // before the player is in the foundry. Driving it later animated the doors
    // while they were being looked at, which is why they never finished closing.
    // First touch of a device snaps it, so this lands as a pose, not a travel.
    sun.add("shut foundry transport",0,{device(kFoundry,0,kHatchShut),device(kFoundry,1,kHatchShut),
        device(kFoundry,2,kHatchShut),device(kFoundry,3,kHatchShut),device(kFoundry,4,kHatchShut)});
    a=sun.add("sun exposure",0,{objective(12),speech(26),spawn(Cohort::sunEast)}); // needs some kind of visit
    b=sun.add("first shade",a,{visit(0x4943FE2EU,39)}); // maybe THIS should be the dependee of a? A should wait until you are walking outside. then again there's speech dependent on this one
    sun.add("shade instruction",b,{speech(27)}); // this should be a visit
    // The native retreat Scene owns its actor cast. No loose requests for slots 0/3/6.
    sun.add("native retreat",a,{scene(kSun,2),scene(kSun,5),scene(kSun,8)});
    c=sun.add("sun bridge",a,{visit(kSun,111)});
    sun.add("bridge combat",c,{spawn(Cohort::sunBridge)});
    d=sun.add("far deck",c,{visit(kSun,112)});
    sun.add("far deck combat",d,{spawn(Cohort::sunWest)});
    const auto secret=sun.add("secret deck",a,{visit(kSun,114)});
    sun.add("secret defender",secret,{spawn(Cohort::sunSecret)});
    const auto doorway=sun.add("far deck exit",d,{visit(kSun,113)});
    sun.add("preload interior",doorway,{spawn(Cohort::readyTwo)});
    e=sun.add("back inside",d,{visit(0x25CFBE03U,6)});
    sun.add("leave sunlight",e,{objective(13),finish()});

    auto& ready=phases[6];ready.name("1AU / Interior corridors");
    a=ready.add("ready room",0,{objective(15),speech(28),spawn(Cohort::readyTwo),control(Mechanic::restrict)}); //speech 28 may be optional?
    b=ready.add("interior reinforcement",a,{reached(Milestone::readyTwoReinforce)});
    c=ready.add("interior second wave",b,{spawn(Cohort::readyTwoReinforce)});
    d=ready.add("interior defenders clear",c,{cleared(Cohort::readyTwo),cleared(Cohort::readyTwoReinforce)});
    e=ready.add("interior safe",d,{control(Mechanic::allow),objective(13)});
    f=ready.add("Ikora farewell area",a,{visit(0x25CFBE03U,15)});
    ready.add("Ikora farewell",f,{speech(30)});
    // This corridor has no locked door: traversal must not wait for stragglers.
    b=ready.add("upper corridor",a,{visit(kReady,52)});
    ready.add("continue upward",b,{control(Mechanic::allow),finish()});

    auto& ascent=phases[7];ascent.name("1AU / Ascent");
    a=ascent.add("chamber",0,{spawn(Cohort::chamber),objective(10)});
    b=ascent.add("meat grinder approach",a,{visit(kAscent,98)});
    c=ascent.add("grinder chamber",b,{spawn(Cohort::meatGrinder),objective(14)});
    f=ascent.add("grinder reinforcement area",c,{reached(Milestone::meatReinforce)});
    ascent.add("grinder second wave",f,{spawn(Cohort::meatReinforce)});
    d=ascent.add("climb out",c,{visit(kAscent,93)});
    ascent.add("ascent reinforcement",d,{spawn(Cohort::ascent),speech(31)}); //this is a hint maybe? if so it should only play after idling around
    // Each retreat leg is a separate authored trigger and scene. Step names are
    // the executor's identity, so the three legs cannot share one name.
    g=ascent.add("first retreat approach",d,{visit(kAscent,94)});
    ascent.add("first native retreat",g,{scene(kAscent,2)});
    g=ascent.add("second retreat approach",d,{visit(kAscent,95)});
    ascent.add("second native retreat",g,{scene(kAscent,5)});
    g=ascent.add("third retreat approach",d,{visit(kAscent,96)});
    ascent.add("third native retreat",g,{scene(kAscent,8)});
    f=ascent.add("Cayde farewell area",d,{visit(kAscent,99)});
    ascent.add("Cayde farewell",f,{speech(32)});
    e=ascent.add("foundry overlook",d,{visit(0x4FB1299CU,15)});
    ascent.add("foundry route",e,{speech(33),finish()});

    auto& foundry=phases[8];foundry.name("1AU / Foundry");
    a=foundry.add("foundry combat",0,{objective(15),control(Mechanic::restrict),spawn(Cohort::foundryEntry)});
    b=foundry.add("foundry floor",a,{reached(Milestone::foundryMid)});
    c=foundry.add("foundry reinforcement",b,{spawn(Cohort::foundryMid)});
    d=foundry.add("deeper foundry",c,{reached(Milestone::foundryFinal)});
    e=foundry.add("Bruiser Thurn",d,{spawn(Cohort::foundryBoss)});
    f=foundry.add("all foundry defenders defeated",e,{cleared(Cohort::foundryEntry),cleared(Cohort::foundryMid),cleared(Cohort::foundryBoss)});
    // Clearing the room opens both doors, both ramrods and the warning light with
    // the centre klaxon, lifts the darkness zone and plays cue 34, as the archive
    // does. The route is not gated on any of it: tube entry hangs off the section's
    // first step, so reaching the transport early is never a dead end.
    g=foundry.add("foundry secured",f,{control(Mechanic::allow),sequence(kFoundry,5)});
    foundry.add("foundry transport opens",g,{device(kFoundry,0,kHatchOpen),device(kFoundry,1,kHatchOpen),
        device(kFoundry,2,kHatchOpen),device(kFoundry,3,kHatchOpen),device(kFoundry,4,kHatchOpen)});
    h=foundry.add("tube entry area",a,{visit(0xA09DC18CU,18)});
    foundry.add("on tube entry", h, { objective(16),speech(34) });
    i=foundry.add("tube departure",h,{spawn(Cohort::accessOne),object(kAccess,6),device(kAccess,0,1.F),device(kAccess,1,1.F)});
    // Arrival provisions Light's End on its own rather than through the combat
    // gate above. A player who reaches the tube early still gets the area
    // populated and still ends the section, instead of stranding the run there.
    j=foundry.add("Light's End arrival area",a,{reached(Milestone::lightsEndArrival)});
    k=foundry.add("Light's End provisioned",j,{spawn(Cohort::accessOne),object(kAccess,6),device(kAccess,0,1.F),device(kAccess,1,1.F)});
    foundry.add("Lights End",k,{finish()});

    auto& access=phases[9];access.name("1AU / Light's End");
    a=access.add("look for weapons",0,{objective(17),spawn(Cohort::security),spawn(Cohort::accessOne),device(kAccess,0,1.F),device(kAccess,1,1.F),device(kAccess,3,0.F),object(kAccess,6)});
    b=access.add("second access ledge",a,{visit(kAccess,88)});
    access.add("access reinforcement",b,{spawn(Cohort::accessTwo)});
    c=access.add("security entrance",a,{visit(0xFC43D98EU,7)});
    d=access.add("security encounter",c,{spawn(Cohort::security),objective(18),device(kAccess,2,1.F),sequence(kAccess,7),speech(38)});
    e=access.add("Electron Controllers defeated",d,{cleared(Cohort::electronControllers)});
    f=access.add("unlock reactor approach",e,{device(kAccess,3,1.F),objective(19)});
    g=access.add("reactor entrance",f,{visit(0xFC43D98EU,6)});
    access.add("toward weapon",g,{speech(40),finish()});

    auto& exchangers=phases[10];exchangers.name("1AU / Thermal exchangers");
    a=exchangers.add("reactor assault",0,{control(Mechanic::restrict),objective(20),spawn(Cohort::east),spawn(Cohort::west),object(kCore,26),object(kCore,34),object(kCore,42)});
    b=exchangers.add("exchanger mechanisms",a,{arm(Interaction::east),arm(Interaction::west),device(kCore,25,1.F),device(kCore,33,1.F)});
    // Each side has entry, reinforcement and final squads. The latter two
    // groups were catalogued but never scheduled. Keep the two fronts independent
    // and let real clearance release each wave without gating the clamshells.
    const auto eastClear=exchangers.add("east entry cleared",a,{cleared(Cohort::east)});
    const auto eastWave=exchangers.add("east reinforcements",eastClear,{spawn(Cohort::eastReinforce)});
    const auto eastWaveClear=exchangers.add("east reinforcements cleared",eastWave,{cleared(Cohort::eastReinforce)});
    exchangers.add("east final defenders",eastWaveClear,{spawn(Cohort::eastFinal)});
    const auto westClear=exchangers.add("west entry cleared",a,{cleared(Cohort::west)});
    const auto westWave=exchangers.add("west reinforcements",westClear,{spawn(Cohort::westReinforce)});
    const auto westWaveClear=exchangers.add("west reinforcements cleared",westWave,{cleared(Cohort::westReinforce)});
    exchangers.add("west final defenders",westWaveClear,{spawn(Cohort::westFinal)});
    c=exchangers.add("one exchanger destroyed",b,{destroyed(1)});
    d=exchangers.add("second front",c,{objective(21),speech(43),spawn(Cohort::west)});
    e=exchangers.add("two exchangers destroyed",d,{destroyed(2)});
    // Fast clamshell kills still release every remaining authored group, as in
    // the archived encounter. Re-requesting a cleared source never respawns it.
    const auto remainingWaves=exchangers.add("remaining reactor defenders",e,{spawn(Cohort::eastReinforce),spawn(Cohort::westReinforce),spawn(Cohort::eastFinal),spawn(Cohort::westFinal)});
    f=exchangers.add("coffin front",remainingWaves,{objective(22),speech(44),spawn(Cohort::coffin),arm(Interaction::coffin),device(kCore,6,0.F),device(kCore,7,0.F)});
    g=exchangers.add("three exchangers destroyed",f,{destroyed(3)});
    // The cell is placed by the clamshell's destruction, so it is already there
    // when the player walks in rather than appearing in front of them.
    h=exchangers.add("core fusion cell placed",g,{object(kCore,18),arm(Interaction::coreCell)});
    exchangers.add("reach core",h,{objective(23),speech(48),device(kCore,1,1.F),device(kCore,8,1.F),control(Mechanic::allow),finish()});

    auto& core=phases[11];core.name("1AU / Core overload");
    a=core.add("enter core chamber",0,{visit(0x0F3D5DD5U,11)});
    b=core.add("core fusion cell",a,{object(kCore,18),arm(Interaction::coreCell),speech(50)});
    c=core.add("core cell held",b,{event(Interaction::coreCell,Event::pickedUp)});
    d=core.add("overload receptacle",c,{objective(24),object(kCore,19),arm(Interaction::coreSink)});
    e=core.add("core cell consumed",d,{event(Interaction::coreSink,Event::inserted)});
    core.add("overload",e,{device(kCore,0,1.F),device(kCore,2,1.F),device(kCore,3,1.F),scene(kCore,52),finish()});

    auto& escape=phases[12];escape.name("1AU / Escape");
    a=escape.add("escape ship",0,{objective(25),speech(51),object(kCore,16),scene(kCore,20)});
    b=escape.add("Vanguard signal",a,{visit(0xAD062E98U,7)});
    c=escape.add("begin City assault",b,{speech(53)});
    d=escape.add("escape rails",a,{visit(0xAD062E98U,17)});
    e=escape.add("home",c|d,{speech(54)});
    f=escape.add("final exchange finishes",e,{spoken(51),spoken(53),spoken(54)});
    escape.add("mission complete",f,{{Operation::complete,kModule,6,Wait::requested}});
}
bool Mission::valid() const noexcept {
    const auto valid=[](const Graph& graph) {
        if(!coo::Executor::valid(graph.definition)) {return false;}
        for(const auto& step:graph.definition.steps) for(const auto& command:step.commands) {
            if(!command.asset.registry || !command.asset.definition) {return false;}
            if(command.operation==coo::Operation::dialogue && (command.argument>=std::size(kDialogue) || !kDialogue[command.argument].durationMs)) {return false;}
        }
        return true;
    };
    return valid(bridgeCheckpoint) && valid(processingCheckpoint)
        && std::all_of(phases.begin(),phases.end(),valid);
}
const Mission& mission() noexcept {static const Mission value;return value;}
}
