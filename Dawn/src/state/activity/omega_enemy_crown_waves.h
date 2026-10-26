#pragma once

#include "omega_enemy_crown_catalog.h"

namespace dawn::state::activity::omega_enemy_crown {

struct WaveGroup final {
    std::uint32_t registry;
    std::uint16_t source;
    std::array<std::uint8_t,2> requested;
    bool member;
    std::uint8_t cycle; // 1 Fallen,2 Hive,3 final-platform Vex.
    std::uint8_t wave; // 0 opening,1 reinforcement,2 proxy/escort,3 post-DPS2 escape.
    bool required{true};
};

/** Reconstructed host counts through exact authored native sources. Retail
 * corroborates the races, named proxies and several simultaneous counts; the
 * final Vex opening/reinforcement are occluded. The accepted first18 Fallen are
 * preserved. All cohorts stay required until native optional retirement is
 * proven: disappearance alone never counts as a combat death. */
inline constexpr std::array<WaveGroup,55> kWaves{{
    {kRegistry,2,{3,0},false,1,0},{kRegistry,3,{3,0},false,1,0},
    {kRegistry,4,{1,1},false,1,0},{kRegistry,5,{1,1},false,1,0},
    {kRegistry,6,{1,1},false,1,0},{kRegistry,7,{1,1},false,1,0},
    {kRegistry,8,{1,1},false,1,0},{kRegistry,9,{1,1},false,1,0},
    {kRegistry,10,{1,0},false,1,1},{kRegistry,11,{2,0},false,1,1},{kRegistry,12,{1,0},false,1,1},
    {kRegistry,13,{3,0},false,1,2},{kRegistry,14,{1,0},false,1,2},{kRegistry,15,{1,0},false,1,2},
    {kRegistry,0,{1,0},true,1,2},

    {kHiveRegistry,7,{3,0},false,2,0},{kHiveRegistry,8,{3,0},false,2,0},
    {kHiveRegistry,9,{2,0},false,2,0},{kHiveRegistry,10,{2,0},false,2,0},
    {kHiveRegistry,11,{2,0},false,2,0},{kHiveRegistry,12,{2,0},false,2,0},
    {kHiveRegistry,13,{2,0},false,2,0},{kHiveRegistry,14,{2,0},false,2,0},
    {kHiveRegistry,15,{1,1},false,2,1},{kHiveRegistry,16,{1,1},false,2,1},{kHiveRegistry,17,{4,0},false,2,1},
    {kHiveRegistry,18,{2,0},false,2,2},{kHiveRegistry,19,{2,0},false,2,2},{kHiveRegistry,20,{2,0},false,2,2},
    {kHiveRegistry,5,{1,0},true,2,2},

    // Panoptes departs while these Cabal remain alive. This escape wave cannot
    // create a required kill barrier before native departure/cannon receipts.
    {kCabalRegistry,0,{1,0},false,2,3,false},{kCabalRegistry,1,{1,0},false,2,3,false},
    {kCabalRegistry,2,{1,0},false,2,3,false},{kCabalRegistry,3,{1,0},false,2,3,false},
    {kCabalRegistry,4,{1,0},true,2,3,false},
    {kCabalRegistry,6,{1,0},false,2,3,false},{kCabalRegistry,7,{1,0},false,2,3,false},
    {kCabalRegistry,8,{1,0},false,2,3,false},{kCabalRegistry,9,{1,0},false,2,3,false},
    {kCabalRegistry,10,{1,0},false,2,3,false},{kCabalRegistry,11,{1,0},false,2,3,false},

    {kVexRegistry,6,{3,0},false,3,0},{kVexRegistry,7,{3,0},false,3,0},{kVexRegistry,8,{3,0},false,3,0},
    {kVexRegistry,9,{2,0},false,3,0},{kVexRegistry,10,{2,1},false,3,0},{kVexRegistry,11,{3,0},false,3,0},
    {kVexRegistry,12,{3,0},false,3,0},
    {kVexRegistry,13,{4,0},false,3,1},{kVexRegistry,14,{2,1},false,3,1},
    {kVexRegistry,15,{2,0},false,3,1},{kVexRegistry,16,{2,0},false,3,1},
    {kVexRegistry,17,{3,0},false,3,2},{kVexRegistry,18,{3,0},false,3,2},{kVexRegistry,4,{1,0},true,3,2},
}};
static_assert([] {
    std::array<unsigned,3> totals{};
    for(std::size_t i=0;i<kWaves.size();++i) {
        const auto& group=kWaves[i];const auto* source=find_spawner(group.source,group.registry);
        if(source==nullptr || group.cycle<1 || group.cycle>3 || group.wave>3
            || group.member!=(source->memberSlot!=0)
            || (source->categories==1 && group.requested[1]!=0)) {return false;}
        const auto count=static_cast<unsigned>(group.requested[0])+group.requested[1];
        if(count==0 || count>63 || (group.member && count!=1)) {return false;}
        totals[group.cycle-1]+=count;
        for(std::size_t j=0;j<i;++j) {
            if(kWaves[j].registry==group.registry && kWaves[j].source==group.source) {return false;}
        }
    }
    return totals==std::array<unsigned,3>{28,44,38};
}());

} // namespace dawn::state::activity::omega_enemy_crown
