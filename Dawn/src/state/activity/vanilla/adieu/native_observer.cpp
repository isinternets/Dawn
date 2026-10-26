#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "runtime.h"
#include "../../../../core/logging/log.h"
#include "../../coo/native_device_authority.h"
#include "../../../../client/hooks/bootflow/gateway_native_read.h"
#include "../../../../client/hooks/bootflow/coo_native_components.h"
#include "../../../../client/hooks/bootflow/omega_native_readable.h"
namespace dawn::state::activity::vanilla::adieu {
namespace {
namespace gn=client::hooks::bootflow::gateway_native;
namespace cn=client::hooks::bootflow::coo_native;
struct NativeComponent {std::uint32_t definition,kind,offset;};
std::uintptr_t image() noexcept {return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));}
template<class T> T at(const std::byte* bytes) noexcept {T v{};std::memcpy(&v,bytes,sizeof v);return v;}
bool prefix(const std::byte* b,NativeComponent c) noexcept {return at<std::uint32_t>(b)==c.definition && at<std::uint32_t>(b+4)==c.kind && at<std::uint64_t>(b+8)==c.offset;}
}
void observe_native_object(void* raw) noexcept {
    const auto req=request();if(!req.frame.enabled) {return;}gn::Read read{image()};
    const auto source=reinterpret_cast<std::uintptr_t>(raw);std::array<std::byte,16> header{};
    if(!read.copy(source,header) || at<std::uint32_t>(header.data()+4)!=0x80809928U) {return;}
    const Binding* binding{};
    for(const auto& a:kBindings) {if(a.asset.type==4 && prefix(header.data(),{a.asset.definition,0x80809928U,a.offset})) {binding=&a;break;}}
    if(!binding) {return;}const auto index=object_index(binding->asset);if(index>=kObjects.size() || !req.frame.managedObjects[index]) return;const auto& desired=req.frame.objects[index];
    if(desired.phase==coo::ObjectPhase::prepare) {
        std::array<std::byte,0x44> bytes{};
        if(read.copy(source+0x180,bytes) && at<std::uint32_t>(bytes.data())==desired.generation && coo::native_device::inactive_state(bytes)) {observe_prepared(req.owner,binding->asset);}return;
    }
    std::uint32_t generation{},committed{},bundle{};std::uint8_t active{};gn::Weak entity{},again{};std::uintptr_t row{};
    if(!desired.create || !read.value(source+0x180,generation) || generation!=desired.generation
        || !read.value(source+0x2F0,committed) || committed!=generation || !read.value(source+0x188,active) || active!=1
        || !read.value(source+0x440,entity) || !read.entity_row(entity,row) || !read.value(row+0x4C,bundle)
        || !read.value(source+0x440,again) || again!=entity || !read.weak(again) || request().owner!=req.owner) {return;}
    observe_object({{req.owner.run,generation},binding->asset,entity.handle,entity.serial});
}
}
