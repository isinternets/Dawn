#include <array>
#include <algorithm>
#include "../src/server/bap/encrypted/push/activity/launchpad_roster.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>
#include <filesystem>
#include <fstream>
#include <vector>
#include "../src/state/activity/Newlight/launchpad/controller.h"
#include "../src/state/activity/Newlight/launchpad/authority.h"
#include "../src/state/activity/Newlight/launchpad/quest.h"
#include "../src/state/activity/Newlight/launchpad/shutter.h"
#include "../src/middleware/datagen/family4/character/layout.h"
#include "../src/middleware/encoding/bit_writer.h"
#include "../src/middleware/encoding/bit_reader.h"

namespace lp=dawn::state::activity::newlight::launchpad;
namespace coo=dawn::state::activity::coo;
namespace character=dawn::middleware::datagen::family4::character;
static unsigned checks{};
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
#include "launchpad_retirement_tests.h"
#include "launchpad_shutter_tests.h"
#include "launchpad_lighting_placed_tests.h"
#include "launchpad_lighting_scene_tests.h"
void verify_packet(const lp::Frame& frame) {
    namespace wire=dawn::middleware::bap::activity_message::sensor_auth_update;
    namespace roster=dawn::server::bap::encrypted::push::activity::launchpad_roster;
    namespace layout=dawn::state::build_data::scenarios;
    struct Storage {
        std::array<layout::RosterGroup,wire::kGroupCapacity> rosterGroups{};
        std::array<wire::BubbleSubBlock,4> rosterSubBlocks{};
        std::array<std::array<std::uint32_t,wire::kBubbleKeyCapacity>,4> rosterSubBlockKeys{};
    };
    auto storage=std::make_unique<Storage>();
    auto snapshot=std::make_unique<wire::Snapshot>();
    layout::Definition definition{};definition.tag=lp::kScenario;definition.bubbleCount=4;
    std::copy(lp::kPackage.begin(),lp::kPackage.end(),definition.name.begin());
    definition.nameLength=static_cast<std::uint8_t>(lp::kPackage.size());
    snapshot->lifetime=3;snapshot->launchpad=frame;snapshot->phaseOneOnly=false;
    snapshot->hasRegion=true;snapshot->region=frame.bubble;snapshot->gameplayClockTicks=frame.gameplayClockTicks;
    snapshot->roster.playerKeyGroup=0x4786C0E0U;
    std::uint32_t failed{};
    CHECK(roster::admit(definition,*storage,snapshot->roster,[](std::uint32_t key,std::uint32_t tag,layout::RosterGroup& group) {
        for(const auto& expected:lp::kGroups) if(expected.key==key && expected.tag==tag) {roster::recovered(group,expected);return true;}
        return false;
    },failed));
    roster::movies(*storage,snapshot->roster,frame.cinematic);
    std::array<std::byte,65536> packet{};std::size_t written{};
    CHECK(wire::encode_sensor_auth_update(*snapshot,packet,written));
    CHECK(written>0 && written<packet.size());
}

void verify_bodies(const lp::Frame& frame) {
    verify_packet(frame);
    for(const auto& binding:lp::kAssets) {
        const auto a=binding.asset;
        CHECK(a.type<=UINT8_MAX);
        const auto type=static_cast<std::uint8_t>(a.type);
        const auto bits=lp::body_bits(frame,a.registry,type,a.slot);
        if(!bits) continue;
        std::vector<std::byte> bytes((bits+7)/8);
        dawn::middleware::encoding::bits::Writer writer(bytes);
        CHECK(lp::write_body(writer,frame,a.registry,type,a.slot));
        if(writer.bit_count()!=bits) std::fprintf(stderr,"body %08X/%u/%u expected=%zu got=%zu\n",a.registry,a.type,a.slot,bits,writer.bit_count());
        CHECK(writer.bit_count()==bits);
    }
}

void verify_objective_delivery(const std::filesystem::path& output) {
    namespace bits=dawn::middleware::encoding::bits;
    coo::ObjectiveService objectives;
    lp::Frame frame{};frame.enabled=true;frame.spawnGeneration=1;
    // Breach -> Walker -> ship signal -> Dock13 exercises all native ring slots,
    // including slot reuse, an unchanged publication, progress and retirement.
    constexpr unsigned rows[]{4,4,5,6,7,7};
    constexpr unsigned selectors[]{0,0,1,2,0,2};
    for(unsigned stage=0;stage<std::size(rows);++stage) {
        const auto row=rows[stage];
        const auto key=row==4?lp::kBreachRoute:row==7?lp::kHangarRoute:lp::kDivideRoute;
        const std::uint16_t slot=row==4?22:row==5 || row==7?8:7;
        coo::Asset marker{};
        for(const auto& n:lp::kNavigation) {if(n.asset.registry==key && n.asset.slot==slot) {marker=n.asset;}}
        CHECK(marker.registry==key);
        objectives.set(lp::kObjectives[row],{marker,{0x811C9DC5U,0,0,0}});
        if(stage==5) {objectives.clear();}
        frame.presentation=objectives.state();frame.assaultDefeated=3;
        std::array<std::byte,601> bytes{};bits::Writer writer(bytes);
        CHECK(lp::write_body(writer,frame,lp::kRoot,68,0));
        CHECK(writer.bit_count()==4802);
        bits::Reader reader(bytes);
        const auto read=[&](std::uint8_t width) {std::uint64_t v{};CHECK(reader.read(width,v));return v;};
        CHECK(reader.skip(110)); // Audience and scope.
        for(unsigned record=0;record<3;++record) {
            const bool active=stage!=5 && record==selectors[stage];
            CHECK(read(32)==(active?lp::kObjectives[row]:0x811C9DC5U));
            CHECK(read(32)==0x80000000U); // Authored variant remains zero.
            CHECK(read(2)==(active?1U:0U));
            CHECK(reader.skip(1+5*64+32));
            for(unsigned value=0;value<4;++value) {
                const auto expected=active && row==5 && value<2
                    ? 0x80000000U+(value?lp::kAssaultTarget:3U):0x7FFFFFFFU;
                CHECK(read(32)==expected);
            }
            CHECK(read(2)==1);CHECK(reader.skip(55));
            CHECK(read(3)==1U); // Released route selector; not the previous display-mode override.
            for(unsigned target=0;target<4;++target) {
                const bool selected=active && target==0;
                CHECK(read(32)==(selected?key:0x811C9DC5U));
                CHECK(read(7)==(selected?48U:0U));
                CHECK(read(16)==(selected?32768U+slot:32767U));
                CHECK(reader.skip(55));
                for(unsigned word=0;word<4;++word) {CHECK(read(32)==0x811C9DC5U);}
                CHECK(read(1)==0);
            }
        }
        CHECK(read(3)==selectors[stage]+1U);
        if(!output.empty()) {
            std::filesystem::create_directories(output);
            const auto prefix=output/("launchpad-objective-"+std::to_string(stage));
            std::ofstream binary(prefix.string()+".bin",std::ios::binary);
            binary.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(binary.good());
            const auto& p=frame.presentation;const auto& m=p.marker;
            std::ofstream metadata(prefix.string()+".txt");
            metadata<<p.event<<' '<<p.active<<' '<<selectors[stage]<<' '<<writer.bit_count()<<' '
                <<m.asset.registry<<' '<<m.asset.type<<' '<<m.asset.slot;
            for(auto word:m.locator) {metadata<<' '<<word;}metadata<<'\n';CHECK(metadata.good());
        }
    }
}

void verify_lighting_graph_activation_edge() {
    const auto& graph=lp::mission().phases[1].definition;
    const auto doors=lp::asset(lp::kBreach,4,3);
    unsigned requests{};bool retiredDuringPreparation{},lateLifecycleChange{};
    for(const auto& step:graph.steps) {
        for(const auto& command:step.commands) {
            if(command.operation!=coo::Operation::device || command.asset!=doors) {continue;}
            ++requests;
            if(step.name=="Breach preparation" && command.argument==0) {retiredDuringPreparation=true;}
            if(step.name!="Breach preparation") {lateLifecycleChange=true;}
        }
    }
    CHECK(requests==1);
    CHECK(retiredDuringPreparation);
    CHECK(!lateLifecycleChange);
}

void verify_lighting_dynamic_edge() {
    namespace bits=dawn::middleware::encoding::bits;
    const auto decode=[](std::uint32_t generation) {
        std::array<std::byte,(lp::lighting::kAuthorityBits+7)/8> bytes{};
        bits::Writer writer(bytes);CHECK(lp::lighting::write(writer,generation));
        CHECK(writer.bit_count()==lp::lighting::kAuthorityBits);
        bits::Reader reader(bytes);CHECK(reader.skip(252));
        std::uint64_t value{};CHECK(reader.read(1,value) && value==1);
        CHECK(reader.read(32,value) && value==0x80805063U);
        for(unsigned channel=0;channel<2;++channel) {CHECK(reader.skip(64+32));}
        CHECK(reader.read(32,value) && value==(generation^0x80000000U));
        CHECK(reader.read(32,value) && value==(generation^0x80000000U));
        CHECK(reader.read(32,value) && value==0x3F800000U);
        CHECK(lp::lighting::accepted(generation,static_cast<std::int32_t>(generation),1.F,1.F));
        CHECK(!lp::lighting::accepted(generation,static_cast<std::int32_t>(generation+1),1.F,1.F));
        CHECK(!lp::lighting::accepted(generation,static_cast<std::int32_t>(generation),0.F,1.F));
        CHECK(!lp::lighting::accepted(generation,static_cast<std::int32_t>(generation),1.F,0.F));
    };
    decode(2);decode(17);
    CHECK(lp::lighting::kSource==lp::asset(lp::kBreach,4,1));
    CHECK(!lp::lighting::accepted(0,0,1.F,1.F));
}

int main(int argc,char** argv) {
    launchpad_retirement_tests::verify();
    launchpad_shutter_tests::verify();
    launchpad_lighting_placed_tests::verify();
    launchpad_lighting_scene_tests::verify();
    verify_lighting_graph_activation_edge();
    verify_lighting_dynamic_edge();
    verify_objective_delivery(argc>1?std::filesystem::path(argv[1]):std::filesystem::path{});
    // Only the rifle doorway is eligible for physical-device binding and
    // duplicate suppression; the same model behind the player stays native.
    for (unsigned copy = 0; copy < 3; ++copy) {
        CHECK(lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.741882F, 17.593933F));
    }
    CHECK(!lp::shutter::matches(false, 0x80C44F2BU, 410.103027F, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C230ABU, 410.103027F, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 390.375061F, -793.076294F, 17.405090F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.0F, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.2F, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.9F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.6F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.741882F, 17.4F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.741882F, 17.8F));
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, nan, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, nan, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.741882F, nan));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, infinity, -788.741882F, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -infinity, 17.593933F));
    CHECK(!lp::shutter::matches(true, 0x80C44F2BU, 410.103027F, -788.741882F, infinity));

    auto controller=std::make_unique<lp::Controller>();
    CHECK(!controller->select(0));
    CHECK(controller->select(71,100));
    const auto owner=controller->owner();
    CHECK(owner.valid());
    CHECK(controller->frame().enabled);
    CHECK(controller->frame().cinematic.phase==lp::cinematics::Phase::preparing);
    CHECK(controller->frame().cinematic.route()==0);
    CHECK(controller->advance(71,100000,true)); // Loading alone cannot time out the unarmed movie.
    CHECK(!controller->frame().fault);
    verify_bodies(controller->frame());
    CHECK(!controller->fly_in_complete({},100010));
    CHECK(controller->fly_in_complete(owner,100010));
    CHECK(controller->frame().cinematic.route()==1);
    CHECK(controller->frame().cinematic.masking_opening(100011));
    CHECK(!controller->arrival(owner,4,100012));
    CHECK(controller->arrival(owner,1,100020));
    CHECK(controller->frame().cinematic.phase==lp::cinematics::Phase::offered);
    CHECK(controller->frame().cinematic.play);
    verify_bodies(controller->frame());
    const auto movie=lp::cinematics::kMovies[0];
    CHECK(!controller->cinematic(owner,{5239,0,1,6,0},100030));
    CHECK(controller->cinematic(owner,{5239,movie.registry,1,6,0},100030));
    CHECK(controller->frame().cinematic.phase==lp::cinematics::Phase::playing);
    CHECK(!controller->frame().cinematic.masking_opening(100031));
    CHECK(controller->cinematic(owner,{3338,movie.registry,2,6,0},101000));
    CHECK(controller->cinematic(owner,{1685,movie.registry,3,6,0},101010));
    CHECK(controller->frame().cinematic.phase==lp::cinematics::Phase::landing);
    CHECK(controller->frame().cinematic.route()==4);
    CHECK(!controller->arrival({},4,101020));
    CHECK(controller->arrival(owner,4,101020));
    CHECK(controller->advance(71,101030,true));
    CHECK(controller->frame().cinematic.phase==lp::cinematics::Phase::gameplay);
    CHECK(!controller->frame().fault);
    verify_bodies(controller->frame());

    // A consumed conversation retains its generation and clears its optional
    // timestamp. Reproduce the first post-cinematic publication and later rows.
    auto dialogueFrame=controller->frame();
    for(std::size_t row=0;row<dialogueFrame.generations.size();++row) {
        dialogueFrame.generations[row]=1;
        dialogueFrame.activeRow=static_cast<std::uint8_t>(row);
        verify_bodies(dialogueFrame);
        dialogueFrame.activeRow=coo::kNoDialogue;
        verify_bodies(dialogueFrame);
    }
    for(std::uint64_t now=101155;now<131030;now+=125) {
        CHECK(controller->advance(71,now,true));
        CHECK(!controller->frame().fault);
        verify_bodies(controller->frame());
    }

    // Entering the Breach retains the logical gate while the physical shutters
    // remain closed until Ghost finishes. The optional overlay stays retired.
    CHECK(controller->region(owner,0));
    for(std::uint64_t now=131155;now<=131530;now+=125) {CHECK(controller->advance(71,now,true));}
    CHECK(controller->frame().section==1 && !controller->frame().fault);
    const auto& logical=controller->frame().native[lp::asset_index(lp::asset(lp::kBreach,4,2))];
    CHECK(logical.managed && logical.desired);
    const auto& overlay=controller->frame().native[lp::asset_index(lp::asset(lp::kBreach,4,3))];
    CHECK(overlay.managed && !overlay.desired && !overlay.active);
    const auto& shutter=controller->frame().native[lp::asset_index(lp::asset(lp::kBreach,23,75))];
    CHECK(shutter.managed && shutter.active && shutter.position==0.F);
    CHECK(!controller->frame().pickups[0].armed);
    verify_bodies(controller->frame());

    // Drive the actual mission/controller handshake, not a synthetic Frame.
    // The wrong source/revision previously passed packet-size tests while
    // trapping Ghost at the switch and leaving the second shutter closed.
    controller->position(71,{390.F,-789.F,17.F});
    std::uint64_t cueTime=131655;
    for(unsigned tick=0;tick<12;++tick,cueTime+=125) {CHECK(controller->advance(71,cueTime,true));}
    CHECK(controller->frame().ghost.phase==lp::ghost::Phase::lights);
    const auto ghostGeneration=controller->frame().native[lp::asset_index(lp::ghost::kSource)].generation;
    const lp::EnemyReceipt ghostActor{71,100,101,ghostGeneration,27,lp::kBreach};
    CHECK(controller->admitted(ghostActor));
    controller->ghost_sample(owner,ghostActor,{3,false});
    for(unsigned tick=0;tick<12;++tick,cueTime+=125) {CHECK(controller->advance(71,cueTime,true));}
    CHECK(controller->frame().lightRequested && !controller->frame().light);
    CHECK(controller->prepared(owner,lp::lighting::kSource));
    const auto lightGeneration=controller->frame().native[lp::asset_index(lp::lighting::kSource)].generation;
    const coo::ObjectReceipt lightReceipt{{71,lightGeneration},lp::lighting::kSource,200,201,202};
    CHECK(controller->object(lightReceipt));
    auto wrongLight=lightReceipt;wrongLight.source=lp::asset(lp::kBreach,4,3);
    CHECK(!controller->lights(owner,wrongLight));
    CHECK(!controller->frame().light && shutter.position==0.F);
    CHECK(controller->lights(owner,lightReceipt));
    CHECK(!controller->lights(owner,lightReceipt)); // no replay
    CHECK(controller->frame().ghost.returnReleased);
    controller->ghost_sample(owner,ghostActor,{8,false});
    for(unsigned tick=0;tick<12;++tick,cueTime+=125) {CHECK(controller->advance(71,cueTime,true));}
    CHECK(controller->frame().light && controller->frame().ghost.ready());
    CHECK(shutter.position==1.F && shutter.generation>1);
    CHECK(!overlay.active && !overlay.desired);
    CHECK(!controller->frame().fault);
    verify_bodies(controller->frame());

    // Native quest flags must start New Light and later release the forced start.
    // Existing vendor overrides and another Guardian's state must survive projection.
    dawn::state::CharacterState guardian{};
    guardian.inventory.count=1;
    guardian.inventory.values[0].quantity=1;
    for(std::size_t step=0;step<lp::quest::kSteps.size();++step) {
        guardian.inventory.values[0].definitionHash=lp::quest::kSteps[step];
        auto object=std::make_unique<character::layout::Object>();
        object->unlockFlagCount=1;object->unlockFlags[0]={144,1,0};
        object->unlockValueCount=1;object->unlockValues[0]={900,0,456};
        lp::quest::project(guardian,*object);
        CHECK(object->acquiredFlags[20]==(step==0?std::byte{2}:std::byte{}));
        CHECK(object->acquiredFlags[59]==(step?std::byte{2}:std::byte{}));
        CHECK(object->unlockFlagCount==5 && object->unlockValueCount==7);
        CHECK(object->unlockFlags[0].slot==144 && object->unlockFlags[0].value==1);
        CHECK(object->unlockValues[0].slot==900 && object->unlockValues[0].value==456);
        lp::quest::project(guardian,*object);
        CHECK(object->unlockFlagCount==5 && object->unlockValueCount==7);
    }
    guardian.inventory.values[0].postmaster=true;
    std::array<std::byte,60> untouched{};untouched[20]=std::byte{1};
    lp::quest::project_start(guardian,untouched);CHECK(untouched[20]==std::byte{1});
    const auto otherGuardian=guardian;
    CHECK(lp::quest::record_escape(guardian));
    CHECK(lp::quest::escaped(guardian) && !lp::quest::escaped(otherGuardian));
    guardian.inventory={}; // Completion survives losing the Pursuit.
    auto completed=std::make_unique<character::layout::Object>();
    completed->acquiredFlags[20]=std::byte{2};
    lp::quest::project(guardian,*completed);
    CHECK(completed->acquiredFlags[20]==std::byte{} && completed->acquiredFlags[59]==std::byte{2});
    guardian.inventory.count=1;guardian.inventory.values[0].definitionHash=lp::quest::kSteps[0];
    guardian.inventory.values[0].quantity=1;
    lp::quest::project(guardian,*completed);
    CHECK(completed->acquiredFlags[20]==std::byte{}); // Stale initial quest cannot relaunch.
    std::printf("Launchpad integration: %u checks passed\n",checks);
}
