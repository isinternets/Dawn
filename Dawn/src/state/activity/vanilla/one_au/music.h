#pragma once
#include "controller.h"

namespace dawn::state::activity::vanilla::one_au::music {
inline constexpr auto kSensor=asset(kRoot,11,1);
inline constexpr std::size_t kBits=128+129*55;
// One authored selector group, 29 sections in bank 80B3C904. Timing follows
// mission milestones; the native bank owns transitions and track playback.
constexpr int section(const Frame& f) noexcept {
    // A wipe that restores the same section leaves the selector unchanged, so the
    // native bank sees no transition and never restarts. Drop the selection while
    // the wipe holds; releasing it is then a real change the bank acts on.
    if(!f.enabled || f.finished || f.cinematic.phase!=cinematics::Phase::gameplay
        || f.recovery.holding()) {return -1;}
    switch(static_cast<Section>(f.section)) {
    case Section::landing:return 0;
    case Section::bridge:return 1; // originally f.interactions[0].completed? 2:1; is this the ghost interaction? regardless track 2 seems empty; checked datto's video, the music never stops
    case Section::processing:return 2; // 3, but datto's video has silence here
    case Section::grinder:return f.presentation.event==kObjectives[9]?4:3; //f.presentation.event==kObjectives[9]?5:4; 
    case Section::tunnel:return 6;
    case Section::sunside:return f.presentation.event==kObjectives[12]?10:8; // this fires too early, needs to wait until door opens / player outside. tried to time to objective, no dice. But if you run for it you wont notice :P
    case Section::ready:return 11;
    case Section::ascent:return 12; //13;
    case Section::foundry:return f.presentation.event==kObjectives[16]?16:13; // 17 : 16
    case Section::access:return f.presentation.event==kObjectives[18]?21:19;
    case Section::exchangers:return f.interactions[5].destroyed || f.interactions[6].destroyed?24:23;
    case Section::core:return 27;
    case Section::escape:return 28;
    default:return -1;
    }
}
template<class W> bool write(W& w,const Frame& f) noexcept {
    const auto selected=section(f);
    if(!w.write(selected<0?0U:1U<<selected,32) || !w.write(0,32) || !w.write(0,32) || !w.write(0,32)) {return false;}
    for(unsigned i=0;i<129;++i) {
        if(!w.write(0x811C9DC5U,32) || !w.write(0,7) || !w.write(0x7FFFU,16)) {return false;}
    }
    return true;
}
}
