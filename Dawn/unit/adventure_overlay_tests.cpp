#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <filesystem>
#include <vector>
#include "core/provenance/build_provenance.h"
#include "state/build_data/cache/records/codec.h"
#include "server/runtime/activity/adventure_mercury_overlays.h"
#include "server/runtime/activity/adventure_opening_publication.h"
#include "middleware/encoding/bit_writer.h"
namespace rec=dawn::state::build_data::cache::records;
namespace overlay=dawn::server::runtime::activity::authored_overlay;
namespace mercury=dawn::server::runtime::activity::adventure::mercury;
namespace layouts=dawn::state::build_data::scenarios;
namespace wire=dawn::middleware::bap::activity_message::sensor_auth_update;
namespace opening=dawn::server::runtime::activity::adventure;
namespace bits=dawn::middleware::encoding::bits;
unsigned checks{};
void check(bool value,const char* what){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
template<class T> bool read(std::ifstream& f,T& value){return bool(f.read(reinterpret_cast<char*>(&value),sizeof value));}
template<class T> void skip(std::ifstream& f,std::uint32_t count){f.seekg(std::uint64_t(count)*sizeof(T),std::ios::cur);}
struct Fixture final {
    std::vector<layouts::Definition> scenarios;
    std::vector<layouts::RosterGroup> groups;
    explicit Fixture(const char* path){
        std::ifstream file(path,std::ios::binary);rec::Header h{};
        check(read(file,h) && h.magic==rec::kCacheMagic && h.version==rec::kCacheFormatVersion,"real current-format cache");
        skip<rec::NamedRecord>(file,h.namedCount);skip<rec::ItemRecord>(file,h.itemCount);
        skip<rec::CollectibleRecord>(file,h.collectibleCount);skip<rec::MaterialRequirementSetRecord>(file,h.materialRequirementSetCount);
        skip<rec::ItemDetailRecord>(file,h.itemDetailCount);skip<rec::SocketPlugRuleRecord>(file,h.socketPlugRuleCount);
        skip<rec::SocketPlugPoolRecord>(file,h.socketPlugPoolCount);skip<rec::SocketPlugMemberRecord>(file,h.socketPlugMemberCount);
        skip<rec::InventoryBucketRecord>(file,h.inventoryBucketCount);skip<rec::SocketEntryListRecord>(file,h.socketEntryListCount);
        skip<rec::SocketEntryTableRecord>(file,h.socketEntryTableCount);skip<rec::AbilityBucketRecord>(file,h.abilityBucketCount);
        skip<rec::ProgressionRecord>(file,h.progressionCount);
        check(h.scenarioCount<=layouts::kDefinitionCapacity && h.rosterGroupCount<=layouts::kRosterGroupCapacity,"bounded real catalog");
        scenarios.resize(h.scenarioCount);groups.resize(h.rosterGroupCount);
        for(auto& s:scenarios){rec::ScenarioRecord r{};check(read(file,r) && rec::decode(r,s),"production scenario decode");}
        for(auto& g:groups){rec::RosterGroupRecord r{};check(read(file,r) && rec::decode(r,g),"production descriptor decode");}
    }
    const layouts::Definition& scenario(std::uint32_t tag)const{
        for(const auto& s:scenarios)if(s.tag==tag)return s;check(false,"fixture scenario present");std::abort();
    }
    const layouts::RosterGroup& group(std::uint32_t key)const{
        for(const auto& g:groups)if(g.registryKey==key)return g;check(false,"fixture group present");std::abort();
    }
};
struct Storage final {
    std::array<layouts::RosterGroup,wire::kGroupCapacity> rosterGroups{};
    std::array<wire::BubbleSubBlock,64> rosterSubBlocks{};
    std::array<std::array<std::uint32_t,96>,64> rosterSubBlockKeys{};
};
wire::Group expose(const layouts::RosterGroup& g){return {g.registryKey,std::span(g.slotTypes).first(g.slotCount),
    std::span(g.slotFlags).first(g.slotCount),std::span(g.slotIndices).first(g.slotCount)};}
wire::Roster baseline(Storage& s,const Fixture& f){
    s.rosterGroups[0]=f.group(0x4786C0E0);s.rosterGroups[1]=f.group(0x2749BAAE);
    s.rosterSubBlockKeys[0][0]=0x2749BAAE;s.rosterSubBlocks[0]={15,std::span(s.rosterSubBlockKeys[0]).first(1)};
    wire::Roster r{};r.groups[0]=expose(s.rosterGroups[0]);r.groups[1]=expose(s.rosterGroups[1]);
    r.groupCount=2;r.topLevelGroupCount=1;r.playerKeyGroup=0x4786C0E0;r.bubbleSubBlocks=std::span(s.rosterSubBlocks).first(1);return r;
}
int main(int argc,char**argv){
    if(argc!=3)return 2;const Fixture fixture(argv[1]);
    std::filesystem::create_directories(argv[2]);
    const auto find=[&](std::uint16_t index,layouts::RosterGroup& result){
        if(index>=fixture.groups.size())return false;result=fixture.groups[index];return true;};
    for(const auto& binding:mercury::kOpeningOverlays){
        const auto& host=fixture.scenario(binding.hostScenario);const auto& selected=fixture.scenario(binding.selectedScenario);
        auto plan=std::make_unique<overlay::Plan>();
        check(overlay::prepare(host,selected,binding,find,*plan),"actual selected root/local pair and complete descriptors");
        auto storage=std::make_unique<Storage>();auto roster=baseline(*storage,fixture);
        const auto* originalTop=roster.groups[0].slotTypes.data();const auto* originalLocal=roster.groups[1].slotTypes.data();
        check(overlay::append(*plan,*storage,roster)==overlay::Result::added,"compose selected authored overlay");
        check(roster.groupCount==4 && roster.topLevelGroupCount==2,"one global root and one local group");
        check(roster.groups[0].slotTypes.data()==originalTop && roster.groups[2].slotTypes.data()==originalLocal,"existing backing descriptors preserved");
        check(roster.groups[1].key==binding.root.key && roster.groups[3].key==binding.local.key,"authored root/local order");
        check(roster.playerKeyGroup==0x4786C0E0 && roster.bubbleSubBlocks[0].bubble==15,"player binding and region scope retained");
        check(roster.bubbleSubBlocks[0].keys.size()==2 && roster.bubbleSubBlocks[0].keys[0]==0x2749BAAE
            && roster.bubbleSubBlocks[0].keys[1]==binding.local.key,"existing bubble keys retained");
        check(overlay::append(*plan,*storage,roster)==overlay::Result::present && roster.groupCount==4,"repeat cannot duplicate overlay");
        const auto ordinal=opening::opening_lifetime_scenario(binding,*plan,roster,120,120);
        check(ordinal==15U,"exact admitted opening publishes authored phase ordinal15");
        for(const auto region:{-1,0,8,96,104,112,119,121,128,512}) {
            check(!opening::opening_lifetime_scenario(binding,*plan,roster,region,120),"foreign current region cannot publish opening");
            check(!opening::opening_lifetime_scenario(binding,*plan,roster,120,static_cast<std::uint32_t>(region)),"manual/foreign arrival cannot publish opening");
        }
        auto altered=roster;altered.groups[1].key^=1;
        check(!opening::opening_lifetime_scenario(binding,*plan,altered,120,120),"missing exact root refuses phase publication");
        altered=roster;altered.groups[3].key^=1;
        check(!opening::opening_lifetime_scenario(binding,*plan,altered,120,120),"missing local refuses phase publication");
        altered=roster;altered.groups[0].key^=1;
        check(!opening::opening_lifetime_scenario(binding,*plan,altered,120,120),"missing common native lifetime refuses publication");
        altered=roster;altered.bubbleSubBlocks={};
        check(!opening::opening_lifetime_scenario(binding,*plan,altered,120,120),"unattached overlay refuses publication");
        auto wrongIdentity=binding;wrongIdentity.root.object^=1;
        check(!opening::opening_lifetime_scenario(wrongIdentity,*plan,roster,120,120),"wrong native object provenance refuses publication");
        plan->prepared=false;
        check(!opening::opening_lifetime_scenario(binding,*plan,roster,120,120),"unprepared plan refuses publication");
        plan->prepared=true;
        wire::Snapshot snapshot{};snapshot.lifetime=3;snapshot.hasRegion=true;snapshot.region=120;
        snapshot.hasSpawnOverride=true;snapshot.spawnSliceSet=120;snapshot.spawnSetHash=0x811C9DC5;
        const auto body=[&]() {
            std::array<std::byte,65> bytes{};bits::Writer writer(bytes);
            check(wire::write_auth_body(writer,snapshot,0x4786C0E0,17,3,false)&&writer.bit_count()==520,"actual production lifetime writer");
            return bytes;
        };
        const auto before=body();snapshot.lifetimeScenarioOrdinal=ordinal;const auto fixed=body();
        for(std::size_t i=0;i<before.size();++i)if(i<9||i>12)check(before[i]==fixed[i],"all other lifetime authority wire bytes preserved");
        for(const auto& item:{std::pair{"lifetime-before.body",before},std::pair{"lifetime-adventure15.body",fixed}}) {
            std::ofstream out(std::filesystem::path(argv[2])/item.first,std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());check(bool(out),"export actual writer for original decoder");
        }
        auto wrong=selected;std::swap(wrong.authoredGroups[15][0],wrong.authoredGroups[15][1]);
        check(!overlay::prepare(host,wrong,binding,find,*plan) && !plan->prepared,"wrong source order rejects and clears plan");
        wrong=selected;wrong.bubbleHashes[15]^=1;
        check(!overlay::prepare(host,wrong,binding,find,*plan),"mismatched host slice rejects");
        wrong=selected;wrong.authoredGroupCounts[15]=3;
        check(!overlay::prepare(host,wrong,binding,find,*plan),"unreviewed extra overlay group rejects");
        auto badBinding=binding;badBinding.selectedPackage="mercury_freeroam";
        check(!overlay::prepare(host,selected,badBinding,find,*plan),"wrong selected package rejects");
        badBinding=binding;badBinding.bubbleHash=0;
        check(!overlay::prepare(host,selected,badBinding,find,*plan),"unqualified host slice rejects");
        badBinding=binding;badBinding.root.key=0x811C9DC5;
        check(!overlay::prepare(host,selected,badBinding,find,*plan),"absent group identity rejects");
        std::array<mercury::OverlaySlot,2> duplicateSlots{binding.root.slots[0],binding.root.slots[0]};
        badBinding=binding;badBinding.root.slots=duplicateSlots;
        check(!overlay::prepare(host,selected,badBinding,find,*plan),"duplicate expected slot identity rejects");
        check(!overlay::prepare(host,selected,binding,[](std::uint16_t,layouts::RosterGroup&){return false;},*plan),"missing catalog rejects");
        check(!overlay::prepare(host,selected,binding,[&](std::uint16_t index,layouts::RosterGroup& result){
            if(!find(index,result))return false;result.descriptorTags[0]^=1;return true;},*plan),"descriptor provenance mismatch rejects");
        check(overlay::prepare(host,selected,binding,find,*plan),"prepare after failed inputs");
        const auto validCount=plan->groups[0].slotCount;
        plan->groups[0].slotCount=static_cast<std::uint16_t>(layouts::kRosterSlotCapacity+1);
        roster=baseline(*storage,fixture);
        check(overlay::append(*plan,*storage,roster)==overlay::Result::invalid && roster.groupCount==2,"malformed plan cannot form an out-of-bounds span");
        plan->groups[0].slotCount=validCount;
        const auto validIndex=plan->groups[0].slotIndices[1];plan->groups[0].slotIndices[1]=plan->groups[0].slotIndices[0];
        check(overlay::append(*plan,*storage,roster)==overlay::Result::invalid && roster.groupCount==2,"malformed plan duplicate slot rejects before writes");
        plan->groups[0].slotIndices[1]=validIndex;
        roster=baseline(*storage,fixture);roster.groupCount=roster.groups.size()-1;
        const auto capacityCount=roster.groupCount;
        check(overlay::append(*plan,*storage,roster)==overlay::Result::capacity && roster.groupCount==capacityCount,"capacity failure has no partial writes");
        roster=baseline(*storage,fixture);std::array<std::uint8_t,1> removal{0};storage->rosterSubBlocks[0].presence=removal;
        check(overlay::append(*plan,*storage,roster)==overlay::Result::conflict && roster.groupCount==2,"explicit native removal state is preserved");
        roster=baseline(*storage,fixture);roster.groups[1]=expose(plan->groups[1]);
        check(overlay::append(*plan,*storage,roster)==overlay::Result::conflict && roster.groupCount==2,"partial previous overlay rejects");
        roster=baseline(*storage,fixture);roster.bubbleSubBlocks={};
        check(overlay::append(*plan,*storage,roster)==overlay::Result::added && roster.bubbleSubBlocks.size()==1,"new selected bubble block");
        roster=baseline(*storage,fixture);storage->rosterSubBlocks[0].bubble=7;
        check(overlay::append(*plan,*storage,roster)==overlay::Result::added && roster.bubbleSubBlocks.size()==2
            && roster.bubbleSubBlocks[0].bubble==7 && roster.bubbleSubBlocks[1].bubble==15,"unrelated bubble retained");
    }
    std::printf("PASS: %u Adventure overlay checks with real cached package records\n",checks);
}
