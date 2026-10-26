#include "../src/middleware/content/packages/tables/adventure_routes.h"
#include "../src/server/runtime/activity/adventure_mercury_capabilities.h"
#include "../src/middleware/encoding/bit_writer.h"
#include "../src/middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../src/server/runtime/activity/persistent_activity.h"
#include "../src/server/runtime/activity/mercury_definition.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace tables=dawn::middleware::content::packages::tables;
namespace route=tables::adventure;
namespace authored=dawn::state::activity::coo::adventure::mercury;
namespace caps=dawn::server::runtime::activity::adventure::mercury;
namespace placement=dawn::server::runtime::activity::placement;
unsigned checks{},failures{};
void check(bool value,const char* message) {
    ++checks;if(!value) {++failures;std::cerr<<"FAIL "<<message<<'\n';}
}
std::vector<std::byte> file(const std::filesystem::path& name) {
    std::ifstream f(name,std::ios::binary|std::ios::ate);
    if(!f) return {};
    const auto length=f.tellg();if(length<0) return {};
    std::vector<std::byte> result(static_cast<std::size_t>(length));
    f.seekg(0);f.read(reinterpret_cast<char*>(result.data()),length);return result;
}
template<class T> void put(std::vector<std::byte>& bytes,std::size_t offset,T value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
void executor_case(const std::filesystem::path& fixtures) {
    namespace a=dawn::server::runtime::activity;
    namespace coo=dawn::state::activity::coo;
    namespace native=a::mercury;
    const std::array<coo::script::Capability,4> registered{
        native::kScriptCapabilities[0],caps::kScriptCapabilities[0],
        caps::kScriptCapabilities[1],caps::kScriptCapabilities[2]};
    auto profile=native::kProfile;profile.id="adventure.fixture";profile.capabilities=registered;profile.parameters={};
    const auto actions=caps::actions(0);
    const a::NativeActivityDefinition definition{"mercury_freeroam",L"adventure_mercury_flags.json",15,
        &profile,std::span(&authored::kRegistry,1),native::kPopulations,caps::kPlacements,actions,native::kPersistentModule};
    std::ifstream f(fixtures/"adventure_mercury_flags.json");
    const std::string text((std::istreambuf_iterator<char>(f)),{});std::string error;
    std::shared_ptr<const coo::script::MissionDocument> document=coo::script::MissionDocument::parse(text,profile,error);
    check(bool(document),"file-selected flag definition parses");if(!document) {std::cerr<<error<<'\n';return;}
    a::PersistentActivity owner;
    check(owner.begin({73,{2}},definition,document,875),"shared persistent executor accepts adventure data");
    check(owner.update(15,false).placements.count==0,"arrival gates placement");
    a::NativeActivityFrame frame{};
    for(int i=0;i<4;++i) frame=owner.update(15,true);
    check(frame.placements.count==3 && frame.populations.count==0,"executor publishes three native flags only");
    for(int i=0;i<25;++i) check(owner.update(15,true).placements.count==3,"persistent update retains requests");
    check(owner.diagnostics().phase==coo::Phase::complete,"startup completion is not activity completion");
    check(owner.update(14,true).placements.count==0 && owner.update(15,true).placements.count==3,"bubble revisit retains native placement selection");
    auto changed=text;const std::string remove=",{\"id\":\"c\",\"binding\":\"intercept\"}";
    const auto at=changed.find(remove);check(at!=std::string::npos,"editable fixture has intercept selection");
    if(at!=std::string::npos) changed.erase(at,remove.size());
    std::shared_ptr<const coo::script::MissionDocument> next=coo::script::MissionDocument::parse(changed,profile,error);
    check(bool(next),"file can omit a registered flag without code changes");
    if(next) {
        a::PersistentActivity replacement;check(replacement.begin({74,{1}},definition,next,876),"fresh owner accepts changed selection");
        for(int i=0;i<4;++i) frame=replacement.update(15,true);
        check(frame.placements.count==2,"changed definition publishes exactly two flags");
        check(owner.update(15,true).placements.count==3,"old owner retains immutable definition");
    }
}
int main(int argc,char** argv) {
    if(argc!=2) {std::cerr<<"usage: adventure_tests FIXTURE_DIRECTORY\n";return 2;}
    const std::filesystem::path fixtures=argv[1];
    executor_case(fixtures);
    const auto bytes=file(fixtures/"81327D63.bin");
    if(bytes.size()!=48200) {std::cerr<<"missing/wrong installed route fixture\n";return 2;}
    const auto publicBytes=file(fixtures/"81327CF0.bin");
    std::array<dawn::state::build_data::activities::Definition,2048> activities{};
    std::size_t activityCount{};
    check(tables::activities::decode(publicBytes,activities,activityCount),"public catalog parses");
    route::Routes result{};
    for(const auto& slot:authored::kSlots) {
        char filename[24]{};std::snprintf(filename,sizeof(filename),"%08X.bin",slot.descriptorTag);
        const auto body=file(fixtures/filename);
        struct Seen {tables::SlotDescriptor descriptor{};std::size_t count{};} seen;
        const auto visitor=[](void* value,const tables::SlotDescriptor& d) noexcept {
            auto& captured=*static_cast<Seen*>(value);captured.descriptor=d;++captured.count;return true;
        };
        check(tables::visit_slot_descriptors(body,slot.descriptorTag,authored::kRegistry.key,visitor,&seen)
            && seen.count==1 && seen.descriptor.sourceTag==slot.descriptorTag
            && seen.descriptor.slotIndex==slot.index && seen.descriptor.slotType==slot.type
            && seen.descriptor.componentClass==slot.componentClass
            && seen.descriptor.senseSchema==slot.senseSchema && seen.descriptor.authSchema==slot.authSchema,
            "production descriptor reader agrees with complete pinned registry");
        seen={};
        check(tables::visit_slot_descriptors(body,slot.descriptorTag,0x12345678,visitor,&seen)
            && seen.count==0,"wrong registry cannot claim descriptor");
    }
    for(std::size_t i=0;i<authored::kBeacons.size();++i) {
        const auto& beacon=authored::kBeacons[i];
        check(route::lookup(bytes,route::kClass,beacon.selector,result)==route::Result::found,"installed selector exists");
        check(result.count==1 && result.choices[0].activity==beacon.publicOrdinal,"authored alternate ordinal");
        const auto& choice=result.choices[0];
        check(choice.tokenCount==5 && choice.tokens[0].operation==1
            && choice.tokens[0].argument==16745+i && choice.tokens[1].operation==2
            && choice.tokens[2].operation==1 && choice.tokens[2].argument==1097
            && choice.tokens[3].operation==2 && choice.tokens[4].operation==4,"opaque eligibility preserved");
        check(beacon.publicOrdinal<activityCount && activities[beacon.publicOrdinal].hash==beacon.activityHash
            && activities[beacon.publicOrdinal].name()==beacon.targetPackage,"exact public identity, no name alias");
        char filename[24]{};std::snprintf(filename,sizeof(filename),"%08X.bin",beacon.descriptor);
        const auto body=file(fixtures/filename);
        std::uint32_t cls{},kind{},selector{},entity{};
        check(tables::activities::read(body,0x614,cls) && cls==0x80804CFC
            && tables::activities::read(body,0x620,kind) && kind==2
            && tables::activities::read(body,0x628,selector) && selector==beacon.selector
            && tables::activities::read(body,0x580,entity) && entity==0x80C0127D,"authored entity/typed selector join");
    }
    placement::wire::Batch output{};
    check(placement::project(caps::kPlacements,15,output) && output.count==3,"shared placement authority accepts flags");
    for(std::size_t i=0;i<3;++i) check(output.entries[i].registry==authored::kRegistry.key
        && output.entries[i].slot==i,"only three selected native slots");
    check(placement::project(caps::kPlacements,14,output) && output.count==0,"wrong bubble does not activate");
    const auto actions=caps::actions(6);
    for(std::size_t i=0;i<3;++i) check(actions[i].capability==i+6
        && actions[i].command.asset.definition==authored::kBeacons[i].descriptor,"definition actions route by index");
    auto wrong=authored::kRegistry;auto slots=authored::kSlots;slots[0].authSchema=0x80804F48;
    wrong.slots=slots;placement::Capability invalid{&wrong,0};
    check(!placement::project(std::span(&invalid,1),15,output) && output.count==0,"wrong native schema rejected");
    std::array<placement::Capability,2> repeated{caps::kPlacements[0],caps::kPlacements[0]};
    check(!placement::project(repeated,15,output),"duplicate native placement rejected");
    check(route::lookup(bytes,route::kClass,0x12345678,result)==route::Result::absent
        && result.count==0,"unknown selector never substitutes a route");
    check(route::lookup(bytes,0x80805B95,authored::kBeacons[0].selector,result)==route::Result::invalid,"wrong tag class");
    // Reject every truncated prefix before the complete selected expression.
    for(std::size_t n=0;n<0x9B28;++n)
        check(route::lookup(std::span(bytes).first(n),route::kClass,authored::kBeacons[0].selector,result)!=route::Result::found
            && result.count==0,"truncated pointer/header/body");
    auto broken=bytes;put(broken,16,INT64_MAX);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::invalid,"overflow relative pointer");
    broken=bytes;put(broken,16,INT64_MIN);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::invalid,"underflow relative pointer");
    broken=bytes;put(broken,0x1C50,authored::kBeacons[0].selector);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::invalid,"ambiguous selector fails");
    broken=bytes;put<std::uint64_t>(broken,0x9AD0,129);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::invalid,"bounded expression");
    broken=bytes;put<std::int16_t>(broken,0x9AE0,-1);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::found
        && result.choices[0].activity==-1,"native absent ordinal retained");
    broken=bytes;put<std::uint64_t>(broken,0x9AD0,0);put<std::uint64_t>(broken,0x9AD8,0);
    check(route::lookup(broken,route::kClass,authored::kBeacons[0].selector,result)==route::Result::found
        && result.choices[0].tokenCount==0,"absent expression preserved, never evaluated");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
