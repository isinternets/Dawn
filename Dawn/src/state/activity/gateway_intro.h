#pragma once
#include "Newlight/launchpad/cinematics.h"
#include "omega_ending_transit_rules.h"
#include "runtime.h"
#include "../build_data/activities/activity_catalog.h"
#include "../../core/logging/log.h"
#include <cstdio>
#include <mutex>
namespace dawn::state::activity::gateway_intro {
namespace native = omega_ending_transit;
namespace cine = newlight::launchpad::cinematics;
// Retail public activity table 81327CF0, entries 289..292. The first two
// entries are video activities and intentionally have no scenario package.
inline constexpr std::int16_t kIntroduction=289, kOsiris=290, kBriefing=291, kMission=292;
inline constexpr std::uint32_t kScenario=0x80B4A063U, kRegistry=0x15F0F5F9U;
inline constexpr std::string_view kPackage="cine_120_frn";
inline bool catalog_valid(std::span<const build_data::activities::Definition> rows) noexcept {
    return rows.size()>kMission && rows[kIntroduction].hash==0x8C2CCA33U
        && rows[kOsiris].hash==0x4614D477U && rows[kBriefing].hash==0x3F00CF4BU
        && rows[kMission].hash==0x5A2E3FF4U && rows[kIntroduction].name().empty()
        && rows[kOsiris].name().empty() && rows[kBriefing].name()==kPackage
        && rows[kMission].name()=="mission_abs";
}
enum class Phase : std::uint8_t {
    idle, videoRequested, videoLoading, videoPlaying, videoReturning,
    preparing, offered, playing, stopping,
    missionRequested, missionLoading, complete, failed
};
struct State {
    Phase phase{}; std::int16_t video{kIntroduction};
    std::uint64_t deadline{}, run{}, member{}, runtime{UINT64_MAX};ActivityInstanceKey activity{};
    native::Teleport host{}; std::uint32_t revision{}; bool play{};
};
struct Frame { std::uint64_t run{}; std::uint32_t revision{}; bool enabled{},play{}; };
class Sequence {
public:
    void begin(std::uint64_t now) noexcept { s={};s.phase=Phase::videoRequested;s.deadline=now+30000; }
    void fail() noexcept { s.play=false;++s.revision;s.phase=Phase::failed;s.deadline=0; }
    void complete() noexcept { s.play=false;s.phase=Phase::complete;s.deadline=0; }
    std::int16_t wanted() const noexcept {
        if(s.phase==Phase::videoRequested) return s.video;
        if(s.phase==Phase::missionRequested && s.host.state==0) return kMission;
        return -1;
    }
    bool queued(std::int16_t index,std::uint64_t now) noexcept {
        if(index<0 || index!=wanted()) return false;
        s.phase=index==kMission?Phase::missionLoading:Phase::videoLoading;
        // Both videos belong to one native chain; movie presentation can stop
        // the 3D camera poll until the briefing begins loading.
        s.deadline=now+(index==kIntroduction?1500000:180000);return true;
    }
    // Retail advances 289 -> 290 -> 291 itself (reason 5, commit 3). The
    // intervening orbit step can start and end in one frame. Observe that
    // chain; never submit a competing selection or require an orbit receipt.
    void video(std::int32_t step,std::int16_t index,bool playing,bool finished,std::uint64_t now) noexcept {
        if(s.phase>=Phase::videoLoading && s.phase<=Phase::videoReturning
            && step==39 && index==kOsiris && playing && !finished) {
            if(s.video!=kOsiris) {s.video=kOsiris;s.phase=Phase::videoPlaying;s.deadline=now+650000;}
        }
        if(step==39 && index==s.video) {
            if(s.phase==Phase::videoLoading && playing && !finished) {s.phase=Phase::videoPlaying;s.deadline=now+650000;}
            if(s.phase==Phase::videoPlaying && !playing && finished) {s.phase=Phase::videoReturning;s.deadline=now+650000;}
        }
    }
    void selected(std::uint64_t run,std::int16_t index,std::uint32_t scenario,std::uint64_t now) noexcept {
        // The exact loaded briefing is the native chain's arrival receipt.
        // Video completion and the one-frame orbit step can both occur between polls.
        if(s.phase<Phase::videoLoading || s.phase>Phase::videoReturning
            || !run || index!=kBriefing || scenario!=kScenario) return;
        s.run=run;s.phase=Phase::preparing;s.deadline=now+90000;s.revision=1;
    }
    Frame frame(std::uint64_t run) const noexcept {
        return {s.run,s.revision,run!=0 && run==s.run && s.phase>=Phase::preparing && s.phase<=Phase::missionLoading,s.play};
    }
    bool incident(std::uint64_t run,const cine::Incident& e,std::uint64_t now) noexcept {
        if(run!=s.run || !run || e.registry!=kRegistry || e.type!=6 || e.slot!=0 || e.runtime==UINT64_MAX) return false;
        if(e.target==5239 && s.phase==Phase::offered) {s.runtime=e.runtime;s.phase=Phase::playing;s.deadline=now+650000;return true;}
        if(e.runtime!=s.runtime) return false;
        if(e.target==3338 && s.phase==Phase::playing) {s.play=false;++s.revision;s.phase=Phase::stopping;s.deadline=now+15000;return true;}
        if(e.target!=1685 || (s.phase!=Phase::playing && s.phase!=Phase::stopping)) return false;
        s.play=false;++s.revision;s.phase=Phase::missionRequested;s.deadline=now+30000;return true;
    }
    native::Authority project(ActivityInstanceKey activity,std::uint64_t run,std::uint64_t member,native::Observation o,std::uint64_t now) noexcept {
        if(!frame(run).enabled || !activity || !member || member==UINT64_MAX) return {};
        if(!s.activity) {
            if(s.phase!=Phase::preparing || o.local.state!=0 || (!o.hasTeleport && o.local!=native::Teleport{})) return {};
            s.activity=activity;s.member=member;auto token=static_cast<std::uint8_t>(o.local.token+1);if(!token) token=1;
            // cine_120_frn bubble 1, authored cinematic state 1 (region 9).
            s.host={1,token,9,kRegistry};
        }
        if(s.activity!=activity || s.member!=member) return {};
        if(o.hasTeleport && o.local.token==s.host.token && o.local.sliceSetIndex==s.host.sliceSetIndex && o.local.sliceSetHash==s.host.sliceSetHash) {
            if(s.host.state==1 && o.local.state==3 && o.hasRegion && o.currentRegion==9) {
                s.host.state=3;s.phase=Phase::offered;s.play=true;++s.revision;s.deadline=now+60000;
            } else if(s.host.state==3 && o.local.state==0) {s.host.state=0;}
        }
        return {s.host,true,s.host.state!=1,s.host.state==0};
    }
    void tick(std::uint64_t now) noexcept {if(s.deadline && now>=s.deadline) fail();}
    const State& state() const noexcept {return s;}
private: State s{};
};
inline std::mutex mutex;
inline Sequence sequence;
inline void report(std::string_view event,const State& s) noexcept {
    std::array<char,192> line{};std::snprintf(line.data(),line.size(),
        "ev=gateway_intro stage=%.*s phase=%u video=%d run=%llu teleport=%d play=%u",
        static_cast<int>(event.size()),event.data(),static_cast<unsigned>(s.phase),s.video,
        static_cast<unsigned long long>(s.run),static_cast<int>(s.host.state),s.play?1U:0U);
    core::log::write(core::log::Channel::client,core::log::Level::info,line.data());
}
inline State state() noexcept {const std::lock_guard lock(mutex);return sequence.state();}
inline bool active() noexcept {const auto p=state().phase;return p>Phase::idle && p<Phase::complete;}
inline bool suppress_loading() noexcept {const auto p=state().phase;return p>Phase::idle && p<Phase::missionLoading;}
inline void begin(std::uint64_t now) noexcept {const std::lock_guard lock(mutex);sequence.begin(now);}
inline void fail() noexcept {const std::lock_guard lock(mutex);sequence.fail();}
inline void complete() noexcept {const std::lock_guard lock(mutex);sequence.complete();}
inline void tick(std::uint64_t now) noexcept {const std::lock_guard lock(mutex);sequence.tick(now);}
inline std::int16_t wanted() noexcept {const std::lock_guard lock(mutex);return sequence.wanted();}
inline bool queued(std::int16_t index,std::uint64_t now) noexcept {const std::lock_guard lock(mutex);return sequence.queued(index,now);}
inline void video(std::int32_t step,std::int16_t index,bool playing,bool finished,std::uint64_t now) noexcept {
    const std::lock_guard lock(mutex);const auto before=sequence.state();sequence.video(step,index,playing,finished,now);
    if(before.phase!=sequence.state().phase || before.video!=sequence.state().video) report("native_video",sequence.state());
}
inline void selected(std::uint64_t run,std::int16_t index,std::uint32_t scenario,std::uint64_t now) noexcept {
    const std::lock_guard lock(mutex);const auto before=sequence.state().phase;sequence.selected(run,index,scenario,now);
    if(before!=sequence.state().phase) report("briefing_selected",sequence.state());
}
inline Frame frame(std::uint64_t run) noexcept {const std::lock_guard lock(mutex);return sequence.frame(run);}
inline bool incident(std::uint64_t run,const cine::Incident& e,std::uint64_t now) noexcept {const std::lock_guard lock(mutex);return sequence.incident(run,e,now);}
inline native::Authority project(ActivityInstanceKey activity,std::uint64_t run,std::uint64_t member,bool exact,native::Observation o,std::uint64_t now) noexcept {
    const std::lock_guard lock(mutex);const auto before=sequence.state();
    const auto result=exact?sequence.project(activity,run,member,o,now):native::Authority{};
    if(before.host!=sequence.state().host || before.phase!=sequence.state().phase) report("briefing_transit",sequence.state());
    return result;
}
inline bool matches(const Frame& f,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {return f.enabled && key==kRegistry && type==6 && slot==0;}
template<class W> bool write(W& w,const Frame& f) noexcept {
    cine::State movie{};movie.owner={f.run,1};movie.revisions[0]=f.revision;movie.play=f.play;return cine::write(w,movie,0);
}
}
