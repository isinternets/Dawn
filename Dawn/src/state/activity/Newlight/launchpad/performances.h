#pragma once
#include "ghost.h"
namespace dawn::state::activity::newlight::launchpad::performances {
namespace animation=ghost::animation;
struct Nest {std::uint16_t slot,source;std::uint32_t sequence;};
// Native graph 80C0FD96: wall-crawl variants, drop and wall-climb. Shanks use
// 80C1872C. Each starts in its authored idle pose and waits for C9B0910D.
inline constexpr Nest kNests[]{
    {105,10,0x260EB5BEU},{106,10,0x8CEA3AB2U},{107,10,0x8CEA3AB1U},
    {108,10,0x8CEA3AB2U},{109,10,0x8CEA3AB1U},{110,10,0x8CEA3AB2U},
    {111,10,0x74E80629U},{112,10,0x74E80629U},{113,19,0x8CEA3AB2U},
    {114,19,0x8CEA3AB1U},{115,19,0x74E80629U},{116,19,0x74E80629U},
    {117,19,0x74E80629U},{118,19,0xF45284BAU},{119,19,0xF45284B9U}};
constexpr const Nest* nest(coo::Asset a) noexcept {
    if(a.registry==kBreach && a.type==42) {for(const auto& n:kNests) {if(n.slot==a.slot) {return &n;}}}return nullptr;
}
constexpr bool staged_shank(coo::Asset a) noexcept {
    return a.registry==kBreach && a.type==2 && (a.slot==25 || a.slot==26);
}
constexpr bool staged_source(coo::Asset a) noexcept {
    return a.registry==kBreach && a.type==1 && (a.slot==10 || a.slot==19);
}
inline constexpr std::size_t kStagedShankBits=180;
template<class W> bool write_staged_shank(W& w,std::uint32_t generation) noexcept {
    if(!generation || generation>0x7FFFFFFFU) {return false;}
    // Retained Type-2 .5: AB2D96 applies value/mask bits 0/1 to the actor's
    // perception controls (A81ED0/A82070). Set them at admission so these two
    // scene actors cannot acquire Ghost. The named member owns admission;
    // loose Type-1 actors have no member binding and never receive these flags.
    // Type-42 still owns their movement.
    const auto begin=w.bit_count();
    return w.write(1,1) && w.write(generation,31) && w.write(0,2) && w.write(1,3) && w.write(1,1)
        && w.write(0,1) && w.write(1,1) && w.write(generation,31) && w.write(3,6) && w.write(3,6)
        && w.write(0,3) && w.write(0x811C9DC5U,32) && w.write(0,7) && w.write(0x7FFF,16)
        && w.write(0,32) && w.write(0,5) && w.write(0,1) && w.write(0,1)
        && w.bit_count()-begin==kStagedShankBits;
}
// These keys resolve to named .sequence.tft assets in the actor's own bank:
// grate climb 80C1C958, left/right vault 80C1C95A/5C, Vandal drop 80C1B511.
// These actors must be owned by their named members. The parent publishes zero
// loose requests, so the entry atom reaches the actor and cannot spawn a copy.
constexpr std::uint32_t entrance(coo::Asset a) noexcept {
    if(a.registry!=kBreach || a.type!=2) {return 0;}
    switch(a.slot) {
    case 31:case 35:case 42:case 43:case 49:case 55:case 56:case 57:return 0x13B6A591U;
    case 33:case 51:return 0xFC44A797U;
    case 47:return 0x2AC3BDBAU;
    case 59:case 61:return 0xB16E26C0U;
    default:return 0;
    }
}
constexpr bool entrance_source(coo::Asset a) noexcept {
    if(a.registry!=kBreach || a.type!=1) {return false;}
    switch(a.slot) {
    case 30:case 32:case 34:case 41:case 46:case 48:case 50:case 54:case 58:case 60:return true;
    default:return false;
    }
}
inline constexpr std::size_t kEntranceBits=coo::native_combatant::kBindBits+coo::native_atom::kProgramBlockBits+39;
template<class W> bool write_entrance(W& w,std::uint32_t generation,std::uint32_t sequence) noexcept {
    if(!generation || generation>0x7FFFFFFFU || !sequence) {return false;}
    return w.write(1,1) && w.write(generation,31) && w.write(0,2) && w.write(1,3) && w.write(1,1)
        && w.write(0,1) && w.write(0,1) && w.write(1,1) && w.write(generation,31) && w.write(0,6) && w.write(1,6)
        && coo::native_atom::write_atom(w,coo::native_atom::sequence(sequence)) && w.write(0,1);
}
}
