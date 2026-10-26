#pragma once
#include "lighting_placed.h"
namespace dawn::state::activity::newlight::launchpad::lighting::scene {
// Installed level 814F0033, row 0: prefab 80F6504D. This is the actual
// eight-bank light spectacle, not the nearby breach_group flicker controller.
inline constexpr std::array<std::uint32_t,4> kHeader{0x80FA2F0AU,0x80803910U,0xA78U,0};
inline constexpr std::uint64_t kPlacement=0x0758CD3B6FE64DCCULL;
inline constexpr std::array<std::uint32_t,4> kSwitchHeader{0x80C7069BU,0x80803910U,0xA78U,0};
struct Binding {
    gn::Weak entity{},device{};
    std::uintptr_t address{};
    friend bool operator==(const Binding&,const Binding&)=default;
};
template<class Read> bool component(Read& read,std::uintptr_t address,std::uint32_t definition,
    std::uint32_t kind,std::int64_t offset,std::uint32_t entity) noexcept {
    gn::Ref header{};std::uint32_t self{},owner{};std::uintptr_t resolved{};
    return read.value(address,header) && header.handle==definition && header.kind==kind && header.offset==offset
        && read.value(address+0x24,self) && read.resolve(self,resolved) && resolved==address
        && read.value(address+0x2C,owner) && owner==entity;
}
template<class Read> bool linked_scene(Read& read,std::uintptr_t binding,std::uint32_t definition,
    std::int64_t offset,std::uint32_t entity) noexcept {
    std::uint32_t handle{};std::int64_t relative{};std::uintptr_t address{};
    return read.value(binding,handle) && read.value(binding+8,relative) && relative==0
        && read.resolve(handle,address) && component(read,address,definition,0x808084E9U,offset,entity);
}
template<class Read> bool capture(Read& read,std::uintptr_t address,Binding& out) noexcept {
    Binding b{};std::uint32_t self{},owner{},property{},actual{},flags{},bundle{};
    std::uint8_t initialized{};std::uintptr_t row{},identity{},publisher{},resolved{};
    std::uint64_t placement{};std::uint32_t linked{};std::int64_t relative{};
    if(!read.value(address+0x24,self) || !read.value(address+0x2C,owner)
        || !component(read,address,kHeader[0],kHeader[1],kHeader[2],owner)
        || !read.make_weak(self,b.device) || !read.make_weak(owner,b.entity) || !read.entity_row(b.entity,row)
        || !read.value(row+0xC,actual) || actual!=owner || !read.value(row+4,flags) || (flags&5U)
        || !read.value(address+0x70,property) || property!=UINT32_MAX
        || !read.value(address+0x74,initialized) || initialized!=1 || !read.value(row+0x4C,bundle)
        || !client::hooks::bootflow::coo_native::component<Read,1024>(read,bundle,owner,0x808090E8U,identity)
        || !component(read,identity,0x80C70CAEU,0x808090E8U,0xE8,owner)
        || !read.value(identity+0x40,placement) || placement!=kPlacement
        || !linked_scene(read,address+0x3B8,0x80F64F8CU,0x540,owner)
        || !client::hooks::bootflow::coo_native::component<Read,1024>(read,bundle,owner,0x80803902U,publisher)
        || !component(read,publisher,0x80F65036U,0x80803902U,0x4E8,owner)
        || !read.value(publisher+0xD8,linked) || linked!=self
        || !read.value(publisher+0xE0,relative) || relative!=0
        || !linked_scene(read,publisher+0x68,0x80F6503FU,0x16D8,owner)
        || !read.weak(b.entity) || !read.weak(b.device) || !read.resolve(self,resolved) || resolved!=address) {return false;}
    b.address=address;out=b;return true;
}
// A weak identity survives native allocation compaction; its raw address does
// not. Resolve the current address BEFORE reading scene state. The old address
// can remain readable but now name another component from the same prefab.
template<class Read> bool refresh(Read& read,const Binding& remembered,Binding& out) noexcept {
    std::uintptr_t address{};Binding current{};
    if(!read.weak(remembered.entity) || !read.weak(remembered.device)
        || !read.resolve(remembered.device.handle,address) || !capture(read,address,current)
        || current.entity!=remembered.entity || current.device!=remembered.device) {return false;}
    out=current;return true;
}
// One real rising edge. Repeated network publications/ticks must not rewind the
// ten-second authored interpolator, restart its sound, or force bank values.
template<class Read> bool pending(Read& read,std::uintptr_t address) noexcept {
    std::int32_t revision{},snap{};float current{},target{};
    return read.value(address+0x960,revision) && revision==-1
        && read.value(address+0x964,snap) && snap==-1
        && read.value(address+0x370,current) && current==0.F
        && read.value(address+0x37C,target) && target==0.F;
}
// DF6510 stores the position revision before entering DF6C70. At the return
// from DF6C70 a matching revision/current/target is a native acceptance receipt;
// the mission's later camera-side acknowledgement must not gate this callback.
template<class Read> bool accepted_switch(Read& read,std::uintptr_t address,
    std::uint32_t generation,float requested,char snap) noexcept {
    if(requested!=1.F || snap!=1 || !generation || generation>=0x7FFFFFFFU) {return false;}
    gn::Ref header{};gn::Weak entity{},device{};std::uintptr_t row{},resolved{};
    std::uint32_t self{},owner{},actual{},flags{},property{};
    std::int32_t revision{},immediate{};float current{},target{};
    std::array<char,13> name{};
    return read.value(address,header) && header.handle==kSwitchHeader[0]
        && header.kind==kSwitchHeader[1] && header.offset==kSwitchHeader[2]
        && read.value(address+0x24,self) && read.resolve(self,resolved) && resolved==address
        && read.make_weak(self,device) && read.value(address+0x2C,owner)
        && read.make_weak(owner,entity) && read.entity_row(entity,row)
        && read.value(row+0xC,actual) && actual==owner && read.value(row+4,flags) && !(flags&5U)
        && read.value(address+0x30,name) && name==std::array<char,13>{'b','r','e','a','c','h','_','g','r','o','u','p',0}
        && read.value(address+0x70,property) && property!=UINT32_MAX
        && read.value(address+0x960,revision) && revision==static_cast<std::int32_t>(generation)
        && read.value(address+0x964,immediate) && immediate==revision
        && read.value(address+0x370,current) && current==1.F
        && read.value(address+0x37C,target) && target==1.F
        && read.weak(entity) && read.weak(device);
}
}
