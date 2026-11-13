#include "controller.h"
#include <algorithm>
#include <cmath>
namespace dawn::state::activity::vanilla::adieu {
bool contains(const Volume& v,Point p) noexcept {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || v.polygon.size()<3
        || p.x<v.min.x || p.x>v.max.x || p.y<v.min.y || p.y>v.max.y || p.z<v.min.z || p.z>v.max.z) return false;
    bool inside{};
    for(std::size_t i=0,j=v.polygon.size()-1;i<v.polygon.size();j=i++) {
        const auto a=v.polygon[j],b=v.polygon[i];const double dx=double(b.x)-a.x,dy=double(b.y)-a.y,px=double(p.x)-a.x,py=double(p.y)-a.y;
        if(dx==0 && dy==0) continue;
        if(std::abs(dx*py-dy*px)<.00001 && px*dx+py*dy>=0 && px*dx+py*dy<=dx*dx+dy*dy) return true;
        if((a.y>p.y)!=(b.y>p.y) && p.x<dx*py/dy+a.x) inside=!inside;
    }
    return inside;
}
bool crosses(const Volume& v,Point from,Point to) noexcept {
    if(contains(v,from) || contains(v,to)) return true;
    if(v.polygon.size()<3) return false;
    double low=0,high=1;
    for(const auto axis:{std::array<double,4>{from.x,to.x,v.min.x,v.max.x},
        std::array<double,4>{from.y,to.y,v.min.y,v.max.y},std::array<double,4>{from.z,to.z,v.min.z,v.max.z}}) {
        const auto delta=axis[1]-axis[0];
        if(std::abs(delta)<1e-8) {if(axis[0]<axis[2] || axis[0]>axis[3]) return false;continue;}
        auto a=(axis[2]-axis[0])/delta,b=(axis[3]-axis[0])/delta;if(a>b) std::swap(a,b);
        low=(std::max)(low,a);high=(std::min)(high,b);if(low>high) return false;
    }
    const auto at=[&](double t) {return Point{float(from.x+(to.x-from.x)*t),float(from.y+(to.y-from.y)*t),float(from.z+(to.z-from.z)*t)};};
    const auto a=at(low),b=at(high);if(contains(v,a) || contains(v,b)) return true;
    const double dx=double(b.x)-a.x,dy=double(b.y)-a.y;
    for(std::size_t i=0,j=v.polygon.size()-1;i<v.polygon.size();j=i++) {
        const auto p=v.polygon[j],q=v.polygon[i];const double ex=double(q.x)-p.x,ey=double(q.y)-p.y;
        const auto d=dx*ey-dy*ex;if(std::abs(d)<1e-8) continue;
        const double px=double(p.x)-a.x,py=double(p.y)-a.y,t=(px*ey-py*ex)/d,u=(px*dy-py*dx)/d;
        if(t>=0 && t<=1 && u>=0 && u<=1) return true;
    }
    return false;
}
void Controller::reset() noexcept {
    lifecycle_.reset();clock_.reset();cinematic_={};frame_={};objects_={};population_={};dialogue_={};objectives_={};
    visited_.reset();handled_.reset();hasPrevious_=collapse_=healing_=false;now_=sceneVoiceUntil_=reunionVoiceUntil_=0;grant_={};
}
bool Controller::select(std::uint64_t run,std::uint64_t now) noexcept {
    if(run && run==owner().run) return !frame_.fault;
    reset();if(!lifecycle_.begin(run,256)) return false;
    if(!objects_.begin(owner(),kObjects) || !frame_.native.begin(owner())) {reset();return false;}
    frame_.enabled=true;frame_.spawnGeneration=owner().value;now_=now;
    cinematic_.begin(owner(),now);project();return true;
}
void Controller::project() noexcept {
    frame_.cinematic=cinematic_.state();frame_.presentation=objectives_.state();frame_.completion=lifecycle_.publication();
    for(std::size_t i=0;i<kObjects.size();++i) frame_.objects[i]=objects_.state(i);
    for(const auto& scene:kScenes) {
        bool ready=true;
        for(const auto member:scene.cast) if(member.type==4) {
            const auto i=object_index(member);
            ready&=i<kObjects.size() && frame_.managedObjects[i] && frame_.objects[i].phase==coo::ObjectPhase::ready;
        }
        if(ready) static_cast<void>(frame_.native.arm(owner(),scene.asset));
    }
}
bool Controller::arrival(coo::Generation gen,std::uint8_t route,std::uint64_t now) noexcept {
    if(!current(gen) || !cinematic_.arrival(gen,route,now)) return false;
    now_=now;hasPrevious_=false;
    if(route==4) {
        frame_.stage=Stage::city;effect(0,true);effect(2,true);effect(19,true);effect(9,true);
        objective(0);music(0);request_scene(101);
    } else if(route==5) {
        frame_.stage=Stage::outskirts;effect(14,false);effect(6,false);objective(3);music(1);
        request_object(126);request_object(127);request_scene(96);request_scene(97);
    } else if(route==6) {
        frame_.stage=Stage::gap;effect(15,false);objective(4);music(2);
        request_scene(95);request_scene(98);request_scene(99);speech(28);
    }
    ++frame_.revision;project();return true;
}
bool Controller::cinematic(coo::Generation gen,const cinematics::Incident& in,std::uint64_t now) noexcept {
    if(!current(gen) || !cinematic_.incident(gen,in.target,in.registry,in.type,in.slot,in.runtime,now)) return false;
    if(in.target==5239 && cinematic_.state().movie==cinematics::kRescue) effect(16,false);
    if(cinematic_.state().phase==cinematics::Phase::complete)
        frame_.finished=lifecycle_.complete_to_orbit(owner());
    hasPrevious_=false;++frame_.revision;project();return true;
}
void Controller::position(coo::Generation gen,Point p) noexcept {
    if(!current(gen) || !gameplay() || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
    const auto dx=p.x-previous_.x,dy=p.y-previous_.y,dz=p.z-previous_.z;
    const bool segment=hasPrevious_ && dx*dx+dy*dy+dz*dz<2500.F;
    for(std::size_t i=0;i<std::size(kVolumes);++i) if(contains(kVolumes[i],p) || (segment && crosses(kVolumes[i],previous_,p))) visited_.set(i);
    previous_=p;hasPrevious_=true;cues();project();
}
bool Controller::seen(std::string_view name) const noexcept {
    for(std::size_t i=0;i<std::size(kVolumes);++i) if(kVolumes[i].name==name) return visited_[i];return false;
}
bool Controller::once(std::string_view name) noexcept {
    for(std::size_t i=0;i<std::size(kVolumes);++i) if(kVolumes[i].name==name && visited_[i] && !handled_[i]) {handled_.set(i);return true;}return false;
}
void Controller::request_object(std::uint16_t slot) noexcept {const auto i=object_index(asset(kMain,4,slot));if(i<kObjects.size()) frame_.managedObjects.set(i);}
void Controller::retire_object(std::uint16_t slot) noexcept {const auto i=object_index(asset(kMain,4,slot));if(i<kObjects.size()) objects_.retire(i);}
void Controller::retire_ghost() noexcept {
    if(frame_.ghostRetired) return;
    frame_.ghostRetired=true;
    static_cast<void>(frame_.native.stop_scene(owner(),asset(kMain,43,101)));
    static_cast<void>(frame_.native.stop_scene(owner(),kFinding));
    retire_object(118);retire_object(119);
    static_cast<void>(frame_.native.ghost_effect(owner(),17,false));
    static_cast<void>(frame_.native.ghost_effect(owner(),20,false));
}
void Controller::retire_section() noexcept {
    if(frame_.native.scene(kFinding)->requested) retire_ghost();
    for(const auto& scene:kScenes) static_cast<void>(frame_.native.stop_scene(owner(),scene.asset));
    for(std::size_t i=0;i<kObjects.size();++i) if(frame_.managedObjects[i]) objects_.retire(i);
}
void Controller::request_scene(std::uint16_t slot,std::uint32_t event) noexcept {
    const auto a=asset(kMain,43,slot);
    if(!frame_.native.request_scene(owner(),a)) {frame_.fault=true;return;}
    const auto i=Presentation::scene_index(a);
    for(const auto member:kScenes[i].cast) if(member.type==4) request_object(member.slot);
    if(event && !frame_.native.input(owner(),a,event)) frame_.fault=true;
}
void Controller::objective(std::size_t i) noexcept {if(i<std::size(kObjectives)) {objectives_.set(kObjectives[i]);frame_.objective=kObjectives[i];++frame_.revision;}}
void Controller::speech(std::uint8_t row) noexcept {dialogue_.enqueue(kDialoguePolicy,row,now_,0,static_cast<std::uint8_t>(frame_.stage),frame_.revision);}
void Controller::music(std::uint8_t ordinal) noexcept {if(!frame_.native.music(owner(),ordinal)) frame_.fault=true;}
void Controller::effect(std::uint16_t slot,bool enabled,bool once) noexcept {if(!frame_.native.effect(owner(),slot,enabled,once)) frame_.fault=true;}
void Controller::combat(std::size_t first) noexcept {
    for(std::size_t i=first;i<first+12;++i) {
        auto& state=frame_.combat[i];if(state.active) continue;
        state.active=true;state.generation=owner().value;state.row=-1;
        population_.enable(i);population_.policy(i,{true,coo::EnemyIntent::combat,kMain,kCombat[i].tactical,-1});
    }
    ++frame_.revision;
}
bool Controller::cleared(std::size_t first) const noexcept {
    for(std::size_t i=first;i<first+12;++i) if(!population_.ready(i,1) || !population_.cleared(i,1)) return false;return true;
}
void Controller::cues() noexcept {
    if(!gameplay() || frame_.fault || frame_.finished) return;
    if(frame_.stage<=Stage::escape) {
        if(once("tv_scene_rooftop")) {
            request_scene(94,0xDD6D986F);
            // 80FE0318: the first input only creates the leader (wait 1EA0).
            // Wait 60D0 still holds his movement until 5A9CAA14. Leaving it
            // closed strands the leader while the other five actors converge.
            request_scene(94,0x5A9CAA14);
            request_scene(105,0x6A1120BD);
        }
        if(once("tv_scene_tank")) request_scene(92,0xB687F07B);
        if(once("tv_shadow_scene")) request_scene(93,0xB687F07B);
        if(once("tv_omg_gogogo")) request_scene(106,0xA07DCECA);
        if(once("tv_stumble_1") || once("tv_stumble_2")) effect(7,true,true);
        constexpr std::string_view exits[]{"tv_ghost_exit_1","tv_ghost_exit_2","tv_ghost_exit_3"};
        constexpr std::uint32_t events[]{0xFBA2AF26,0xFBA2AF25,0xFBA2AF24};
        if(!frame_.healed) for(std::size_t i=0;i<3;++i) if(once(exits[i])) request_scene(101,events[i]);
        if(frame_.stage==Stage::city && seen("tv_ghost_finding")) {request_scene(102);frame_.stage=Stage::finding;}
        if(frame_.stage==Stage::finding && !collapse_ && seen("tv_player_collapse")) {
            collapse_=true;request_scene(102,0x86685186);effect(8,true,true);
        }
        if(collapse_ && !healing_ && frame_.native.output(kFinding,0x3DCFB316)) {
            healing_=true;effect(18,true,true);frame_.sequences.set(5);
            frame_.gameplayClockTicks=clock_.sample(now_);
            frame_.sequenceStartTicks[5]=frame_.gameplayClockTicks;
            static_cast<void>(frame_.native.ghost_effect(owner(),17,true));
        }
        const auto* finding=frame_.native.scene(kFinding);
        if(healing_ && !frame_.ghostRetired && frame_.native.output(kFinding,kReunionDepartureCue))
            static_cast<void>(frame_.native.ghost_effect(owner(),20,true));
        if(collapse_ && finding && finding->performanceFinished && !frame_.healed) {

            frame_.healed=true;frame_.stage=Stage::escape;
            for(const auto slot:{0,2,7,8,19}) effect(static_cast<std::uint16_t>(slot),false);
            effect(3,true,true);effect(12,true,true);effect(6,true);objective(2);music(1);
            // Control returns before the reunion speech finishes. Keep its
            // selector and Ghost alive until the separate voice boundary below.
        }
        // The native departure cue follows the reunion. Requiring bank row 16
        // deadlocks the City because that optional action can remain dormant.
        // The selector can destroy itself in the same tick as its last action,
        // losing both that speech sample and the departure event. Its qualified
        // native completed flag also establishes the end of the scene.
        if (frame_.healed && (frame_.native.output(kFinding, kReunionDepartureCue) || (finding && finding->completed))
            && reunionVoiceUntil_ && now_ >= reunionVoiceUntil_) { retire_ghost(); }
        if (frame_.healed && once("tv_dlg_city_exit")) { speech(20);   };
        if(frame_.ghostRetired && seen("tv_goto_climb") && cinematic_.transition(owner(),1,now_)) {retire_section();effect(14,true);hasPrevious_=false;}
    } else if(frame_.stage==Stage::outskirts || frame_.stage==Stage::campCombat) {
        if(once("tv_falcon_climb")) request_scene(96,0x159DFE1B);
        if(once("tv_find_guardians_low") || once("tv_find_guardians_high")) {speech(24);objective(3);}
        if(frame_.weaponGranted && frame_.stage==Stage::outskirts) {
            frame_.stage=Stage::campCombat;combat(0);request_scene(104,0x50954B78);
        }
        if(frame_.stage==Stage::campCombat && cleared(0)) {
            objective(4);speech(27);
            if(once("tv_follow_falcon")) request_scene(97,0xF849C1B2);
            if(seen("tv_goto_mountain") && cinematic_.transition(owner(),2,now_)) {retire_section();effect(15,true);hasPrevious_=false;}
        }
    } else if(frame_.stage<=Stage::canyon) {
        if(once("tv_falcon_1")) request_scene(95,0x159DFE1B);
        if(once("tv_falcon_3")) request_scene(98,0x159DFE1B);
        if(once("tv_music_traveler")) music(4); // there should be a cabal ship here!
        if(once("tv_beast_spawn")) {frame_.stage=Stage::bowlCombat;music(5);combat(12);request_scene(103,0x286C14D7);}
        // The authored route out of the bowl remains traversable. Reaching it
        // advances the escape without inventing deaths for surviving enemies.
        if (frame_.stage == Stage::bowlCombat && (cleared(12) || seen("tv_goto_haw"))) { frame_.stage = Stage::canyon; objective(5); music(6); }
        if(frame_.stage==Stage::canyon) {
            if(once("tv_falcon_4")) request_scene(99,0xF849C1B2);
            //if(once("tv_dlg_canyon_1")) speech(29); // this is cut content!!
            if(once("tv_music_fall")) music(6);
            if(seen("tv_goto_arrival") && cinematic_.finish_gameplay(owner(),now_)) {
                frame_.stage=Stage::ending;objectives_.clear();retire_section();effect(16,true);
                // Native cinematics own their soundtrack; traversal music stops
                // only here, after the final cliff, never at the section changes.
                music(0);hasPrevious_=false;
            }
        }
    }
}
bool Controller::advance(std::uint64_t run,std::uint64_t now,bool ready) noexcept {
    if(!frame_.enabled || run!=owner().run) return false;
    now_=now;frame_.gameplayClockTicks=clock_.sample(now);cinematic_.advance(now);
    frame_.fault|=cinematic_.state().phase==cinematics::Phase::failed;
    if(ready && current(owner()) && gameplay()) {
        cues();dialogue_.advance(kDialoguePolicy,owner().value,now,
            now<sceneVoiceUntil_ || (frame_.healed && !frame_.ghostRetired),frame_,frame_.revision,true);
    }
    project();return true;
}
bool Controller::scene(coo::Generation gen,coo::Asset a,const scene_wire::Output& r) noexcept {
    if(!current(gen) || !gameplay() || !frame_.native.observe(gen,a,r)) return false;
    cues();++frame_.revision;project();return true;
}
bool Controller::playback(const PlaybackReceipt& r,std::uint64_t now) noexcept {
    if(!current(r.request.owner) || !gameplay()) return false;
    const auto* state=frame_.native.scene(r.request.scene);const auto old=state?state->speech:std::bitset<32>{};
    if(!frame_.native.playback(r)) return false;
    for(std::size_t i=0;i<std::size(kDialogue);++i) if((r.speech[i] || r.speechFinished[i]) && !old[i]) {
        const auto until=now+kDialogue[i].durationMs+250;
        sceneVoiceUntil_=(std::max)(sceneVoiceUntil_,until);
        // A finished action can first be observed after dispatch. Reserve its
        // full authored voice window as well, so an early action completion or
        // missed startup sample cannot cut off the tail of the audio.
        if(r.request.scene==kFinding && i>=kReunionFirstDialogueRow && i<=kReunionDialogueRow)
            reunionVoiceUntil_=(std::max)(reunionVoiceUntil_,until);
    }
    now_=now;cues();project();return true;
}
bool Controller::prepared(coo::Generation gen,coo::Asset a) noexcept {
    const auto i=object_index(a);if(!current(gen) || i>=kObjects.size() || !frame_.managedObjects[i] || !objects_.prepared(gen,i)) return false;
    ++frame_.revision;project();return true;
}
bool Controller::object(const coo::ObjectReceipt& r) noexcept {
    const auto i=object_index(r.source);if(!current(owner()) || r.owner.run!=owner().run || i>=kObjects.size() || !frame_.managedObjects[i]) return false;
    if(!objects_.observe(i,r,true,0.F,1)) return false;project();return true;
}
bool Controller::use(coo::Generation gen,coo::Asset a,const middleware::bap::activity_message::object_sense::Output& r) noexcept {
    if(!current(gen) || !gameplay() || frame_.stage!=Stage::outskirts || a!=kPickup || frame_.weaponUsed
        || !r.alive || !r.present || !r.hasUse || !r.used || r.generation<=0) return false;
    const auto i=object_index(a);const auto binding=objects_.owner(i);
    if(!binding.valid() || binding.owner.value!=static_cast<std::uint32_t>(r.generation) || objects_.state(i).phase!=coo::ObjectPhase::ready) return false;
    grant_={binding};frame_.weaponUsed=true;++frame_.revision;return true;
}
GrantRequest Controller::grant_request() const noexcept {
    return current(owner()) && gameplay() && frame_.stage==Stage::outskirts && frame_.weaponUsed && !frame_.weaponGranted
        && grant_.binding==objects_.owner(object_index(kPickup))?grant_:GrantRequest{};
}
bool Controller::granted(const GrantRequest& r,std::uint64_t instance) noexcept {
    if(!instance || !r.valid() || grant_request()!=r) return false;
    frame_.weaponGranted=true;retire_object(126);retire_object(127);effect(21,true,true);effect(22,true,true);
    cues();++frame_.revision;project();return true;
}
bool Controller::admitted(const EnemyReceipt& r) noexcept {
    if(!current(owner()) || !gameplay()) return false;
    for(std::size_t i=0;i<kCombat.size();++i) if(r.registry==kMain && r.source==kCombat[i].source) {
        auto& state=frame_.combat[i];
        if(!state.active || state.cleared || r.run!=owner().run || r.generation!=state.generation) return false;
        if(state.sourceOwner!=UINT32_MAX && state.sourceOwner!=r.owner) {
            const auto generation=state.generation+1;
            if(!lifecycle_.reserve_through(owner(),generation) || !population_.restart(i)) {frame_.fault=true;return false;}
            state={};state.active=true;state.generation=generation;state.sourceOwner=r.owner;
            population_.policy(i,{true,coo::EnemyIntent::combat,kMain,kCombat[i].tactical,-1});
            ++frame_.revision;return false;
        }
        const auto result=population_.admit(kCombat,r,owner().run,state.generation);
        if(result==coo::Admission::overflow) frame_.fault=true;
        if(result==coo::Admission::accepted) {state.sourceOwner=r.owner;return true;}
    }
    return false;
}
bool Controller::died(const EnemyReceipt& r) noexcept {
    if(!current(owner()) || !gameplay()) return false;
    for(std::size_t i=0;i<kCombat.size();++i) if(r.registry==kMain && r.source==kCombat[i].source) {
        auto& state=frame_.combat[i];
        if(state.sourceOwner!=r.owner || !population_.died(r,owner().run,state.generation)) return false;
        state.cleared=population_.cleared(i,1);cues();++frame_.revision;project();return true;
    }
    return false;
}
bool Controller::submitted(coo::Generation gen,std::uint32_t bank,std::uint8_t row,std::uint32_t generation,std::uint64_t now) noexcept {
    return current(gen) && dialogue_.submitted(kDialoguePolicy,bank,row,generation,now,frame_,frame_.revision);
}
bool Controller::source(coo::Generation gen,coo::Asset a,const middleware::bap::activity_message::source_sense::Output& r) noexcept {
    if(!current(gen) || !gameplay() || a.type!=1 || !(r.present&2U) || r.costMask&0xFF000000U) return false;
    for(std::size_t i=0;i<kCombat.size();++i) if(a.registry==kCombat[i].registry && a.slot==kCombat[i].source) {
        auto& state=frame_.combat[i];if(!state.active || state.cleared || r.counters[1]!=state.generation) return false;
        for(unsigned n=0;n<24;++n) if(r.costMask&(1U<<n)) {state.known|=1U<<n;state.costs[n]=r.costs[n];}
        std::int8_t best=-1;std::uint8_t cost=127;
        for(std::int8_t n=0;n<24;++n) if((state.known&(1U<<n)) && state.costs[n]<cost) {best=n;cost=state.costs[n];}
        if(best==state.row) return false;
        state.row=best;population_.policy(i,{true,coo::EnemyIntent::combat,kMain,kCombat[i].tactical,best});++frame_.revision;return true;
    }
    return false;
}
}
