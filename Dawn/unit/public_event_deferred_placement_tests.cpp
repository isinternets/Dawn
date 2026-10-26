#include "server/runtime/activity/public_event_deferred_placement_feedback.h"
#include "client/hooks/bootflow/public_event_deferred_placement_capture.h"
#include "client/hooks/bootflow/public_event_point_interface_capture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
namespace f=dawn::server::runtime::activity::public_event::deferred_placement;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
std::vector<std::byte> read(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);check(bool(in),"native fixture exists");
    const auto size=in.tellg();check(size>0,"native fixture nonempty");std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);check(bool(in),"native fixture read");return bytes;
}
template<class Container,class T> void put(Container& bytes,std::size_t at,T value){std::memcpy(bytes.data()+at,&value,sizeof value);}
void capture_cases(const f::Ticket& ticket,const std::vector<std::byte>& before,const std::vector<std::byte>& after,
    const std::vector<std::byte>& package,std::array<std::byte,0x70> authority,std::vector<std::byte> body,
    std::array<std::byte,0x98> entity){
    namespace capture=dawn::client::hooks::bootflow::public_event_deferred_placement_capture;
    auto component=before,asset=package;std::uintptr_t deny{};bool selfAllowed=true,resolveAllowed=true,bodyAllowed=true,weakAllowed=true;
    capture::Identity self{{0x100011U,0x1000},17};
    const auto reader=[&](std::uintptr_t address,std::span<std::byte> destination){
        if(address==deny)return false;
        const auto region=[&](std::uintptr_t base,std::span<const std::byte> bytes){
            if(address<base || address-base>bytes.size() || destination.size()>bytes.size()-(address-base))return false;
            std::copy_n(bytes.begin()+(address-base),destination.size(),destination.begin());return true;
        };
        return region(0x30000,component) || region(0x50000,asset) || region(0x60000,authority);
    };
    const auto resolver=[&](std::uint32_t handle,std::int64_t offset,std::uintptr_t& output){
        if(!resolveAllowed)return false;
        if(handle==ticket.definition && offset==ticket.definitionOffset){output=0x50000+static_cast<std::uintptr_t>(offset);return true;}
        if(handle==3 && offset==0){output=0x60000;return true;}return false;
    };
    const auto identity=[&](std::uintptr_t address,capture::Identity& output){if(!selfAllowed || address!=0x30000)return false;output=self;return true;};
    const auto bodyReader=[&](std::uintptr_t address,std::span<std::byte> destination){
        if(!bodyAllowed || address!=0x60000 || destination.size()!=body.size())return false;
        std::copy(body.begin(),body.end(),destination.begin());return true;
    };
    const auto weak=[&](std::span<const std::byte> current,std::uint32_t& child,std::array<std::byte,0x98>& output){
        if(!weakAllowed || f::field<std::uint64_t>(current,0x440)!=0x200000001ULL)return false;
        child=2;output=entity;return true;
    };
    const capture::Binding binding{ticket,33};capture::Context context{};
    const auto begin=[&](){return capture::begin(binding,0x30000,reader,resolver,identity,bodyReader,context);};
    check(begin()==capture::Result::accepted,"creator begin copies exact definition/visual/authority/self");const auto original=context;
    for(auto address:{0x30000U,0x504C8U,0x50570U,0x50580U,0x60000U}){
        deny=address;check(begin()!=capture::Result::accepted && !context.binding.epoch,"unreadable precreation region rejected");deny=0;
    }
    selfAllowed=false;check(begin()==capture::Result::self,"missing common-pool self fails before original");selfAllowed=true;
    resolveAllowed=false;check(begin()==capture::Result::definition,"missing authored definition mapping rejected");resolveAllowed=true;
    bodyAllowed=false;check(begin()==capture::Result::body,"unreadable authority body rejected");bodyAllowed=true;
    asset[0x570]^=std::byte{1};check(begin()==capture::Result::visual,"malformed visual array rejected");asset=package;
    asset[0x578]^=std::byte{1};check(begin()==capture::Result::visual,"wrong visual row schema rejected");asset=package;
    asset[0x5F0]^=std::byte{1};check(begin()==capture::Result::visual,"wrong authored point GUID rejected");asset=package;
    context=original;component=after;f::Observation output{};
    const auto finish=[&](bool created=true){return capture::finish(context,0x30000,created,41,reader,resolver,identity,bodyReader,weak,output);};
    check(finish() && output.ticket==ticket,"original successful creation passes complete capture contract");
    check(!finish(false),"unchanged false original result emits no creation receipt");
    for(auto address:{0x30000U,0x30170U,0x504C8U,0x50580U,0x60000U}){
        deny=address;check(!finish(),"unreadable postcreation region rejected");deny=0;
    }
    self.source.member++;check(!finish(),"full source salt changed across creation");self.source.member--;
    self.componentLink++;check(!finish(),"common source link changed across creation");self.componentLink--;
    authority[0x20]^=std::byte{1};check(!finish(),"native authority lifetime changed across creation");authority[0x20]^=std::byte{1};
    asset[0x510]^=std::byte{1};check(!finish(),"authored definition changed across creation");asset=package;
    asset[0x590]^=std::byte{1};check(!finish(),"authored placement transform changed across creation");asset=package;
    body[0x170-1]^=std::byte{1};check(!finish(),"authority backing bytes changed across creation");body[0x170-1]^=std::byte{1};
    weakAllowed=false;check(!finish(),"failed full native weak resolution rejected");weakAllowed=true;
    put(entity,0x0C,0x2002U);check(!finish(),"reused low-index child rejected");put(entity,0x0C,2U);
    check(finish(),"stable native capture still qualifies");
    const auto accepted=output;capture::Context current{};std::array<std::byte,0x98> retainedEntity{};
    check(capture::begin(binding,0x30000,reader,resolver,identity,bodyReader,current)==capture::Result::accepted,"later exact source can refresh creation lease");
    const auto retained=[&](const f::Observation& value){return capture::retained(current,value,reader,resolver,identity,bodyReader,weak,retainedEntity);};
    check(retained(accepted),"later read retains independently accepted child");
    auto wrong=accepted;wrong.ticket.event++;check(!retained(wrong),"later matching bytes cannot satisfy wrong event");
    wrong=accepted;wrong.source.member++;check(!retained(wrong),"later source salt must match creator");
    wrong=accepted;wrong.weakChild++;check(!retained(wrong),"later weak serial must match creator");
    weakAllowed=false;check(!retained(accepted),"retired native child cannot retain readiness");weakAllowed=true;
    check(retained(accepted),"stable retained lease restored");
}

void bridge_interface_cases(f::Ticket ticket){
    namespace bridge=dawn::server::runtime::activity::public_event::deferred_bridge;
    namespace point=dawn::client::hooks::bootflow::public_event_point_interface_capture;
    ticket.pointComponent=0x80F58FAC;ticket.pointInterfaceOffset=0x1F80;
    bridge::Mailbox box;check(box.bind(ticket),"exact gate lease binds");auto b=box.lookup(ticket.definition).binding;
    check(box.bind(ticket) && box.lookup(ticket.definition).binding==b,"repeated projection retains epoch");
    auto wrong=ticket;wrong.owner.incarnation.value++;check(!box.bind(wrong),"definition cannot steal retained world lease");
    auto other=ticket;other.definition++;other.slot++;check(box.bind(other),"distinct gate shares owner");
    f::Observation creation{ticket,{0x100011,17,0x1000},21,0x200000001,2};
    check(!box.point_ready(b,creation,22,0x300000001),"point cannot precede native creation");
    auto stale=b;stale.epoch++;check(!box.created(stale,creation),"stale epoch rejects native creation");
    auto invalid=creation;invalid.source.offset=-1;check(!box.created(b,invalid),"missing native source rejects creation");
    invalid=creation;invalid.source.componentLink++;check(!box.created(b,invalid),"source common pool mismatch rejects creation");
    check(box.created(b,creation),"native creation retained once");check(!box.created(b,creation),"duplicate native create is not renewal");
    invalid=creation;invalid.child++;check(!box.point_ready(b,invalid,22,0x300000001),"different child cannot satisfy point dependency");
    check(!box.point_ready(b,creation,21,0x300000001),"earlier point sequence rejected");
    check(box.point_ready(b,creation,22,0x300000001),"point availability follows exact accepted creation");
    check(!box.point_ready(b,creation,23,0x300000001),"repeated point lookup emits no second readiness");
    box.release({ticket.owner.sessionId,{ticket.owner.incarnation.value+1}});check(box.lookup(ticket.definition).ready,"different world retirement preserves lease");
    box.release(ticket.owner);check(!box.lookup(ticket.definition).binding.epoch && !box.lookup(other.definition).binding.epoch,"actual owner retirement clears all its gates");
    check(box.bind(ticket),"fresh postretirement lease can bind");check(!box.lookup(ticket.definition).created && !box.lookup(ticket.definition).ready && !box.lookup(ticket.definition).creation.sequence,"fresh binding cannot inherit retired native readiness");check(!box.created(b,creation),"old epoch cannot reenter replacement lease");
    std::array<std::byte,0x98> entity{};put(entity,0xC,creation.child);put(entity,0x90,ticket.pointGuid);put(entity,0x4C,3U);
    std::array<std::byte,48> output{};put(output,0,ticket.pointComponent);put(output,4,0x80809C50U);put(output,8,ticket.pointInterfaceOffset);
    put(output,16,0x80FEB394U);put(output,24,0x300000001ULL);put(output,40,0x80806730U);
    std::array<std::byte,0x28> group{};put(group,4,ticket.entityDefinition);put(group,0x18,UINT32_MAX);
    std::array<std::byte,0x200> loaded{};put(loaded,0x58,1ULL);put(loaded,0x60,0xA0LL);
    put(loaded,0xFC,0x80809FBDU);put(loaded,0x100,1ULL);put(loaded,0x108,0x80809C22U);
    put(loaded,0x11C,0x80806730U);put(loaded,0x120,ticket.pointComponent);put(loaded,0x124,0x80809C50U);
    put(loaded,0x128,ticket.pointInterfaceOffset);put(loaded,0x130,0x80FEB394U);
    bool readable=true,resolvable=true,weakValid=true;std::uint64_t found{};
    const auto reader=[&](std::uintptr_t address,std::span<std::byte> to){
        if(!readable)return false;
        if(address==0x30000 && to.size()==group.size()){std::copy(group.begin(),group.end(),to.begin());return true;}
        if(address>=0x40000 && address-0x40000<=loaded.size() && to.size()<=loaded.size()-(address-0x40000)){
            std::copy_n(loaded.begin()+(address-0x40000),to.size(),to.begin());return true;}
        return false;};
    const auto resolve=[&](std::uint32_t handle,std::int64_t offset,std::uintptr_t& to){
        if(!resolvable || offset)return false;if(handle==ticket.entityDefinition){to=0x40000;return true;}
        if(handle!=3)return false;to=0x30000;return true;};
    const auto weak=[&](std::uint64_t pair,std::uint32_t& to){if(!weakValid || pair!=0x300000001ULL)return false;to=3;return true;};
    const auto qualify=[&](){return point::qualify(creation,entity,output,reader,resolve,weak,found);};
    check(qualify() && found==0x300000001ULL,"exact authored interface from child group qualifies");
    for(auto offset:{0U,4U,8U,16U,24U,28U,32U,40U}){output[offset]^=std::byte{1};check(!qualify(),"wrong output member or group weak rejected");output[offset]^=std::byte{1};}
    for(auto offset:{0xCU,0x90U,0x4CU}){entity[offset]^=std::byte{1};check(!qualify(),"foreign child or group rejected");entity[offset]^=std::byte{1};}
    entity[4]=std::byte{4};check(!qualify(),"retired child rejected at interface");entity[4]=std::byte{};
    group[0]=std::byte{2};check(!qualify(),"disabled point group rejected");group[0]=std::byte{};
    group[4]^=std::byte{1};check(!qualify(),"foreign authored entity group rejected");group[4]^=std::byte{1};
    readable=false;check(!qualify(),"unreadable group rejected");readable=true;resolvable=false;check(!qualify(),"unmapped group rejected");resolvable=true;
    weakValid=false;check(!qualify(),"stale group serial rejected");weakValid=true;check(qualify(),"valid point qualification restored");
}
int main(int argc,char** argv){
    check(argc==3,"usage: native-deferred-fixture-directory package-output-directory");
    const std::filesystem::path fixtures=argv[1],packages=argv[2];
    struct Gate{const char* tag;std::uint32_t definition,entity;std::uint64_t guid;std::uint16_t slot;};
    constexpr std::array<Gate,4> gates{{
        {"80F5E481",0x80F5E481,0x80F58FAE,0xB09EA2FC12589314,62},
        {"80F5E484",0x80F5E484,0x80F58FC6,0xAACDE2714D597398,63},
        {"80F5E487",0x80F5E487,0x80F58F7D,0x9022BFEEA9D3E6BE,64},
        {"80F5E48A",0x80F5E48A,0x80F58F96,0xE3CCC1D399517D7C,65}}};
    struct Mode{int generation,prior;bool owner,factory,retained,accepted;};
    constexpr std::array<Mode,6> modes{{{1,0,true,true,false,true},{1,0,true,false,false,false},{1,0,false,true,false,false},
        {1,1,true,true,true,false},{1,2,true,true,true,false},{INT32_MIN,0,true,true,false,false}}};
    for(const auto& gate:gates)for(const auto& mode:modes){
        const auto stem=std::string(gate.tag)+"-generation"+std::to_string(mode.generation)+"-prior"+std::to_string(mode.prior)
            +"-owner"+std::to_string(mode.owner)+"-factory"+std::to_string(mode.factory)+"-retained"+std::to_string(mode.retained);
        auto before=read(fixtures/(stem+".before")),after=read(fixtures/(stem+".after")),body=read(fixtures/(stem+".authority"));
        auto definition=read(fixtures/(stem+".definition"));const auto package=read(packages/(std::string(gate.tag)+".bin"));
        check(package.size()>0x610,"actual gate package includes visual");
        std::vector<std::byte> visual(package.begin()+0x580,package.begin()+0x610);
        f::Ticket ticket{{0x9EAA300100200001ULL,{1}},2,3,4,5,0xC8229B2B,gate.definition,1,0x4C8,gate.slot,15,gate.entity,gate.guid};
        check(f::valid(ticket),"authored deferred ticket");
        // Only common-pool and retained native authority identities are private
        // capture fixtures. Generation/selector/weak-child changes are original
        //9F2F30/9EFFC0 output; these bytes remain unchanged from the native run.
        for(auto* bytes:{&before,&after}){put(*bytes,0x20,17U);put(*bytes,0x170,3U);}
        std::array<std::byte,0x70> authority{};put(authority,0,ticket.registry);authority[4]=std::byte{4};
        put(authority,6,ticket.slot);put(authority,0xC,0x8080992FU);put(authority,0x68,std::uint32_t{ticket.bubble});
        std::array<std::byte,0x98> entity{};put(entity,0x0C,2U);put(entity,0x90,gate.guid);
        const bool called=mode.owner && mode.prior<mode.generation;
        f::Capture c{ticket,{0x100011U,17U,0x1000},19,f::kProducerRva,2,before,after,authority,body,definition,visual,entity,called,true,true,called&&mode.factory};
        f::Observation output{};
        const auto qualify=[&](){return f::qualify(ticket,c,output);};
        check((qualify()==f::Result::accepted)==mode.accepted,"original deferred construction/retention outcome");
        if(!mode.accepted){check(output.child==UINT32_MAX,"rejected fixture emits no child");continue;}
        check(output.ticket==ticket && output.source==c.source && output.child==2 && output.weakChild==0x200000001ULL,
            "accepted receipt retains exact immutable identity and full weak child");
        capture_cases(ticket,before,after,package,authority,body,entity);
        bridge_interface_cases(ticket);
        const auto mutate=[&](auto& bytes,std::size_t at,const char* label){const auto value=bytes[at];bytes[at]^=std::byte{1};
            check(qualify()!=f::Result::accepted,label);bytes[at]=value;};
        for(auto at:{0U,4U,8U,0x20U,0x170U}){mutate(before,at,"prior native identity mismatch");mutate(after,at,"current native identity mismatch");}
        for(auto at:{0U,4U,0x30U,0x34U,0x36U,0x38U,0x44U,0x48U,0x58U,0x94U})mutate(definition,at,"wrong loaded authored definition");
        for(auto at:{0U,4U,6U,0xCU,0x18U,0x68U})mutate(authority,at,"wrong authority owner or schema");
        for(auto at:{0U,4U,8U,9U,0xCU,0x10U,0x14U,0x16U,0x20U,0x24U,0x28U,0x30U,0x40U}){
            mutate(body,at,"different authority command rejected");mutate(before,0x180+at,"command not adopted before deferred update");
            mutate(after,0x180+at,"adopted authority changed during update");}
        for(auto at:{0x2F0U,0x2F8U,0x2FCU,0x444U})mutate(after,at,"invalid native creation commit");
        mutate(visual,0,"wrong authored entity");mutate(visual,0x70,"wrong authored GUID");
        mutate(entity,0xC,"full child identity changed");mutate(entity,0x90,"child GUID changed");
        entity[4]=std::byte{4};check(qualify()==f::Result::child,"retired child rejected");entity[4]=std::byte{};
        c.weakChildResolved=false;check(qualify()==f::Result::identity,"weak salt mismatch or lookup failure rejected");c.weakChildResolved=true;
        c.stableNativeIdentity=false;check(qualify()==f::Result::identity,"unqualified pool/lifetime rejected");c.stableNativeIdentity=true;
        c.originalForwarded=false;check(qualify()==f::Result::identity,"no original callback no receipt");c.originalForwarded=true;
        c.originalCreated=false;check(qualify()==f::Result::identity,"original false result cannot create receipt");c.originalCreated=true;
        c.producerRva=0x9F19F0;check(qualify()==f::Result::identity,"local apply cannot masquerade as deferred construction");c.producerRva=f::kProducerRva;
        c.source.member++;check(qualify()==f::Result::identity,"common-pool link mismatch rejected");c.source.member--;
        c.source.offset=-1;check(qualify()==f::Result::identity,"absent self source rejected");c.source.offset=0x1000;
        c.ticket.owner.incarnation.value++;check(qualify()==f::Result::identity,"stale activity incarnation rejected");c.ticket=ticket;
        c.ticket.selectionRevision++;check(qualify()==f::Result::identity,"wrong selection revision rejected");c.ticket=ticket;
        c.ticket.event++;check(qualify()==f::Result::identity,"wrong event run rejected");c.ticket=ticket;
        c.ticket.generation=0;check(qualify()==f::Result::identity,"legacy rally generation excluded");c.ticket=ticket;
        c.ticket.bubble=14;check(qualify()==f::Result::identity,"foreign bubble rejected");c.ticket=ticket;
        c.sequence=0;check(qualify()==f::Result::identity,"missing capture sequence rejected");c.sequence=19;
        for(auto* bytes:{&c.before,&c.after,&c.authorityObject,&c.authorityBody,&c.definition,&c.visual,&c.entity}){
            const auto original=*bytes;*bytes=original.first(original.size()-1);check(qualify()!=f::Result::accepted,"short native capture rejected");*bytes=original;}
        {const auto prior=before;put(before,0x444,3U);check(qualify()==f::Result::child,"existing child replacement needs retirement proof");before=prior;}
        check(qualify()==f::Result::accepted,"rejected variants did not corrupt retained receipt");
    }
    std::cout<<"PASS "<<checks<<" deferred placement checks\n";
}
