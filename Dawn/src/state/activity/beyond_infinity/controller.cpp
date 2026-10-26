#include "controller.h"
#include "../coo/native_mission_forest_authority.h"
#include "transit_contacts.h"
#include "navigation.h"
#include <cmath>
namespace dawn::state::activity::beyond_infinity {
bool valid_document(const coo::script::Views& views) noexcept {
    return views.valid && views.missionId=="beyond_infinity" && views.profileId==kProfile.id
        && !views.phases.empty() && views.mission.modules.size()==1
        && views.mission.modules[0].asset==kModule && views.mission.modules[0].id==1
        && coo::script::authorized(views,kProfile);
}
bool contains(const Volume& v,Point p) noexcept {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || p.x<v.min.x || p.x>v.max.x || p.y<v.min.y || p.y>v.max.y || p.z<v.min.z || p.z>v.max.z
        || v.vertices.size()<3) { return false; }
    bool in{};
    for(std::size_t i=0,j=v.vertices.size()-1;i<v.vertices.size();j=i++) {
        const auto a=v.vertices[j],b=v.vertices[i];
        const double dx=double(b.x)-a.x,dy=double(b.y)-a.y,px=double(p.x)-a.x,py=double(p.y)-a.y;
        if(dx==0 && dy==0) { continue; }
        if(std::abs(dx*py-dy*px)<0.00001 && px*dx+py*dy>=0 && px*dx+py*dy<=dx*dx+dy*dy) { return true; }
        if((a.y>p.y)!=(b.y>p.y) && p.x<dx*py/dy+a.x) { in=!in; }
    }
    return in;
}
void Controller::reset() noexcept {
    executor_.cancel(*this);composition_.reset();lifecycle_.reset();views_=nullptr;run_=now_=escapeSpeechEnd_=0;
    clock_.reset();
    started_=arrived_=false;objectives_={};dialogue_={};lens_={};plate_={};chargingRevision_=0;scenes_={};sceneComplete_.reset();sceneSpeechStarted_={};sceneSpeechComplete_={};sceneCueStarted_={};sceneCueComplete_={};
    transitArrived_.reset();submitted_.reset();voiceEnd_={};seen_.reset();inside_.reset();frame_={};
}
bool Controller::select(const coo::script::Views& views,std::uint64_t run) noexcept {
    if(!run || !valid_document(views)) { reset();return false; }
    if(run_==run) { return views_==&views; }
    reset();
    // Every managed object has a dormant preparation followed by an active generation.
    // Reserve revisions for both Forest visits and teardown within this owner.
    if(!lifecycle_.begin(run,64)) { return false; }
    views_=&views;run_=run;frame_.spawnGeneration=lifecycle_.owner().value;return true;
}
bool Controller::entered(coo::Asset asset,bool current) const noexcept {
    for(std::size_t i=0;i<std::size(kVolumes);++i) {
        if(kVolumes[i].asset==asset) { return current?inside_[i]:seen_[i]; }
    }
    return false;
}
void Controller::position(std::uint64_t run,Point point) noexcept {
    if(!views_ || run!=run_) { return; }
    inside_.reset();frame_.transitContact=0;
    for(std::uint8_t route=1;route<=5;++route) {
        if(transit::route_contact(route,point.x,point.y,point.z)) { frame_.transitContact|=static_cast<std::uint8_t>(1U<<route); }
    }
    for(std::size_t i=0;i<std::size(kVolumes);++i) { if(contains(kVolumes[i],point)) { inside_.set(i); } }
    if(!arrived_) { arrived_=!views_->observationStart || entered(views_->observationStart->asset,true); }
    if(arrived_) { seen_|=inside_; }
    update_navigation();
    frame_.wellEntered=entered({0xDA02FEF1,0x80F4618A,60,9},true);
    const bool previous=frame_.plateOccupied;
    frame_.plateOccupied=false;
    for(std::size_t i=0;i<std::size(kVolumes);++i) {
        if(kVolumes[i].asset.registry==0x233E7149U && kVolumes[i].asset.slot==119) { frame_.plateOccupied=inside_[i]; }
    }
    // A completed charge has broken the shield. Leaving only cancels a charge
    // that has not yet reached the native endpoint.
    if(previous!=frame_.plateOccupied && !frame_.lensDestroyed && !frame_.lensExposed) {
        if(frame_.plateRevision==UINT32_MAX) { frame_.enabled=false;return; }
        ++frame_.plateRevision;chargingRevision_=0;frame_.lensExposed=false;++frame_.revision;
    }
    well_channels();
}
void Controller::well_channels() noexcept {
    // Beam visibility is independent of occupancy. The lens channel changes
    // only after the native plate effect confirms its completed charge.
    for(const auto slot:{std::uint16_t{38},std::uint16_t{40},std::uint16_t{41}}) {
        const auto* binding=find(0x233E7149U,23,slot);if(!binding) { continue; }
        auto& state=frame_.native[asset_index(binding->asset)];
        if(!state.managed || !state.desired) { continue; }
        const bool active=well_channel(slot,frame_);
        if(active!=state.active && state.generation<32766U
            && lifecycle_.reserve_through(lifecycle_.owner(),state.generation+1U)) {
            state.active=active;++state.generation;++frame_.revision;
        }
    }
}
void Controller::request(coo::Asset asset,bool active) noexcept {
    const auto index=asset_index(asset);if(index==std::size(kAssets)) { return; }
    auto& state=frame_.native[index];
    if(state.managed && state.desired==active) { return; }
    if(!state.managed) {
        state.managed=true;state.generation=frame_.spawnGeneration;
        state.prepared=asset.type!=4;
    } else if(state.active!=active && state.prepared) { ++state.generation; }
    state.desired=active;state.active=state.prepared && active;
    if(asset.registry==0x233E7149U && asset.type==23 && (asset.slot==38 || asset.slot==40 || asset.slot==41)) {
        state.active=active && well_channel(asset.slot,frame_);
    }
    ++frame_.revision;
}
bool Controller::prepared(coo::Generation owner,coo::Asset asset) noexcept {
    const auto index=asset_index(asset);
    if(owner.run!=run_ || !frame_.enabled || index==std::size(kAssets) || asset.type!=4) { return false; }
    auto& s=frame_.native[index];
    if(!s.managed || s.prepared || owner.value!=s.generation) { return false; }
    s.prepared=true;if(s.desired) { ++s.generation;s.active=true; }++frame_.revision;return true;
}
bool Controller::lens(const LensReceipt& receipt,bool dead) noexcept {
    const auto& state=frame_.native[asset_index(kLens)];
    if(!receipt.valid() || !frame_.enabled || !state.active || receipt.owner.run!=run_
        || receipt.owner.value!=state.generation || frame_.lensDestroyed) { return false; }
    if(!dead) { return lens_.bind(receipt); }
    // A prior live owner and completed native plate charge are required.
    // Entry latches and timers never substitute for the native health transition.
    if(!frame_.lensExposed) { return false; }
    lens_.expose();if(!lens_.destroyed(receipt)) { return false; }
    frame_.lensDestroyed=true;well_channels();++frame_.revision;return true;
}
bool Controller::plate_pose(const PlateReceipt& r,server::runtime::activity::mission_device_pose::Sample sample) noexcept {
    if(!r.valid() || r!=plate_ || !frame_.enabled || r.owner.run!=run_)return false;
    const auto& native=frame_.native[asset_index(kPlate)];
    if(!native.active || native.generation!=r.owner.value)return false;
    return server::runtime::activity::mission_device_pose::observe(frame_.plateCapture.pose,sample);
}
bool Controller::bind_plate(const PlateReceipt& receipt) noexcept {
    const auto& state=frame_.native[asset_index(kPlate)];
    if(!receipt.valid() || !frame_.enabled || !state.active || receipt.owner.run!=run_
        || receipt.owner.value!=state.generation || frame_.lensDestroyed || plate_.valid()) { return false; }
    plate_=receipt;chargingRevision_=0;++frame_.revision;return true;
}
bool Controller::plate(const PlateReceipt& receipt,std::uint32_t revision,float value,bool complete) noexcept {
    const auto& state=frame_.native[asset_index(kPlate)];
    if(!receipt.valid() || receipt!=plate_ || !frame_.enabled || !state.active || receipt.owner.run!=run_
        || receipt.owner.value!=state.generation || !frame_.plateOccupied || frame_.lensDestroyed
        || revision!=frame_.plateRevision || !std::isfinite(value) || value<0.F || value>1.F) { return false; }
    // First observe this charge running below full. A stale completion output
    // from an earlier entry cannot satisfy a new occupancy revision.
    if(!complete && value<1.F) { chargingRevision_=revision;return false; }
    if(!complete || value!=1.F || chargingRevision_!=revision || frame_.lensExposed) { return false; }
    // Reserve both exposure and the following re-shield before changing the
    // box. Extend the current lease without reusing retired-run revisions.
    const auto& lensChannel=frame_.native[asset_index(find(0x233E7149U,23,41)->asset)];
    if(!lensChannel.managed || !lensChannel.desired || lensChannel.generation>32764U
        || !lifecycle_.reserve_through(lifecycle_.owner(),lensChannel.generation+2U)) { return false; }
    frame_.lensExposed=true;well_channels();++frame_.revision;return true;
}
bool Controller::scene(const SceneReceipt& receipt,bool complete) noexcept {
    const auto index=scene_index(receipt.asset),native=asset_index(receipt.asset);
    if(!receipt.valid() || !frame_.enabled || receipt.owner.run!=run_ || index==std::size(kScenes)
        || native==std::size(kAssets) || !frame_.native[native].active
        || receipt.owner.value!=frame_.native[native].generation) { return false; }
    auto& owner=scenes_[index];
    if(owner.valid() && owner.owner.value!=receipt.owner.value) {
        if(complete) { return false; }
        if(index==0) { escapeSpeechEnd_=0; }
        owner={};sceneComplete_.reset(index);sceneSpeechStarted_[index].reset();sceneSpeechComplete_[index].reset();sceneCueStarted_[index].reset();sceneCueComplete_[index].reset();
    }
    if(!owner.valid()) { if(complete) { return false; }owner=receipt;++frame_.revision;return true; }
    if(receipt!=owner || !complete || sceneComplete_[index]) { return false; }
    sceneComplete_.set(index);++frame_.revision;return true;
}
bool Controller::scene_speech(const SceneReceipt& receipt,std::uint8_t row,std::uint32_t state) noexcept {
    const auto index=scene_index(receipt.asset),native=asset_index(receipt.asset);
    if(!receipt.valid() || receipt.owner.run!=run_ || !frame_.enabled || row>=49 || (state!=1 && state!=2)
        || index==std::size(kScenes) || native==std::size(kAssets) || scenes_[index]!=receipt
        || !frame_.native[native].active || frame_.native[native].generation!=receipt.owner.value) { return false; }
    bool authored{};for(const auto& speech:kScenes[index].speech) { if(speech.row==row) { authored=true; } }
    if(!authored || frame_.sceneRequests[index].silent) { return false; }
    if(state==1) {
        if(sceneSpeechStarted_[index][row]) { return false; }
        sceneSpeechStarted_[index].set(row);return true;
    }
    if(!sceneSpeechStarted_[index][row] || sceneSpeechComplete_[index][row]) { return false; }
    sceneSpeechComplete_[index].set(row);++frame_.revision;return true;
}
bool Controller::transit(coo::Generation owner,std::uint8_t route) noexcept {
    if(!frame_.enabled || owner!=lifecycle_.owner() || !route || route>=transitArrived_.size()
        || frame_.transitRoute!=route || transitArrived_[route]) { return false; }
    transitArrived_.set(route);seen_=inside_;++frame_.revision;return true;
}
bool Controller::scene_cue(const SceneReceipt& receipt,std::uint8_t id,std::uint32_t state,std::uint32_t starts) noexcept {
    const auto index=scene_index(receipt.asset),native=asset_index(receipt.asset);
    if(!receipt.valid() || receipt.owner.run!=run_ || !frame_.enabled || id>=64 || !starts || (state!=1 && state!=2)
        || index==std::size(kScenes) || native==std::size(kAssets) || scenes_[index]!=receipt
        || !frame_.native[native].active || frame_.native[native].generation!=receipt.owner.value) { return false; }
    bool authored{};for(const auto& cue:kScenes[index].cues) { if(cue.id==id) { authored=true; } }
    if(!authored) { return false; }
    // Instant native event emitters can start and finish inside one tick. Their
    // authored input counter proves activation even if state 1 was not sampled.
    const bool changed=!sceneCueStarted_[index][id] || (state==2 && !sceneCueComplete_[index][id]);
    if(!changed) { return false; }
    sceneCueStarted_[index].set(id);if(state==2) { sceneCueComplete_[index].set(id); }
    // Root action23 completes its authored 33.5-second delay on the same
    // E238DE82 edge that starts child80EC0872. Child row45 starts at33.34392s.
    // Retain the entire bank duration plus250ms after this later native cue.
    // The earlier escape event (29.5s), host elapsed time, and root completion
    // cannot start this tail. Native receipt identity is checked above.
    if(index==0 && id==23 && state==2 && !escapeSpeechEnd_) {
        escapeSpeechEnd_=now_+kDialogue[45].durationMs+250U;
    }
    ++frame_.revision;return true;
}
bool Controller::submitted(std::uint64_t run,std::uint32_t bank,std::uint8_t row,std::uint32_t generation,std::uint64_t now) noexcept {
    if(!views_ || run!=run_ || !started_ || !dialogue_.submitted(views_->dialogue,bank,row,generation,now,frame_,frame_.revision)) { return false; }
    submitted_.set(row);voiceEnd_[row]=dialogue_.voice_until();return true;
}
bool Controller::publish(const coo::Command& command) noexcept {
    if(!views_ || !graph() || !coo::script::valid_token(graph()->definition,executor_,command)) { return false; }
    const auto& spec=command.spec;
    switch(spec.operation) {
    case coo::Operation::objective:
        dialogue_.objective(views_->dialogue,spec.argument,frame_,frame_.revision);objectives_.set(spec.argument,navigation::marker(spec.argument,frame_));break;
    case coo::Operation::dialogue:
        dialogue_.enqueue(views_->dialogue,static_cast<std::uint8_t>(spec.argument),now_,0,frame_.section,frame_.revision);break;
    case coo::Operation::scene: {
        const auto index=scene_index(spec.asset);if(index==std::size(kScenes)) { return false; }
        if(spec.argument!=1 && spec.argument!=3) { return false; }
        frame_.sceneRequests[index].silent=spec.argument==3;
        for(const auto& cast:kScenes[index].cast) { if(cast.type==1 || cast.type==4) { request(cast,true); } }
        request(spec.asset,true);break;
    }
    case coo::Operation::population:
    case coo::Operation::device: request(spec.asset,spec.argument!=0);break;
    case coo::Operation::mechanic:
        if(spec.asset.type==43) {
            const auto index=scene_index(spec.asset),native=asset_index(spec.asset);
            if(index==std::size(kScenes) || native==std::size(kAssets) || !frame_.native[native].active) { return false; }
            bool authored{};for(const auto event:kScenes[index].inputs) { if(event==spec.argument) { authored=true; } }
            if(!authored) { return false; }
            auto& request=frame_.sceneRequests[index];
            for(const auto event:request.inputs()) { if(event==spec.argument) { return true; } }
            if(request.count==request.events.size()) { return false; }
            request.events[request.count++]=spec.argument;++frame_.revision;break;
        }
        if(spec.argument>=21 && spec.argument<=25) {
            const auto route=static_cast<std::uint8_t>(spec.argument-20);
            if(route!=frame_.transitRoute && frame_.transitRoute && !transitArrived_[frame_.transitRoute]) { return false; }
            if(route<frame_.transitRoute) { return false; }
            frame_.transitRoute=route;
        }
        else if(spec.argument==10) { seen_=inside_; }
        else if(spec.argument==11 || spec.argument==12) {
            const auto pass=static_cast<std::uint8_t>(spec.argument-10);
            // The first visit belongs to the observations collected during
            // the reveal. Only a different, later visit retires those latches.
            if(frame_.forestPass && frame_.forestPass!=pass) { seen_=inside_; }
            if(frame_.forestPass!=pass)frame_.forestReady=false;
            frame_.forestPass=pass;
            frame_.forestSeed=coo::native_generator::mission_seed(owner().run,owner().value,pass);
        }
        else { return false; }++frame_.revision;break;
    case coo::Operation::complete:
        if(!lifecycle_.complete(lifecycle_.owner())) { return false; }frame_.finished=true;objectives_.clear();break;
    case coo::Operation::observation: break;
    default: return false;
    }
    return true;
}
bool Controller::observed(const coo::CommandSpec& spec) const noexcept {
    if(views_ && views_->condition(spec)) { return views_->evaluate(spec,[this](const auto& child) { return observed(child); }); }
    if(spec.asset==kModule && spec.argument>=31 && spec.argument<=35) { return (frame_.transitContact&(1U<<(spec.argument-30)))!=0; }
    if(spec.asset==kModule && spec.argument>=21 && spec.argument<=25) { return transitArrived_[spec.argument-20]; }
    if(spec.asset==kDialogueAsset) { return spec.argument<49 && submitted_[spec.argument] && now_>=voiceEnd_[spec.argument]; }
    if(spec.asset==kLens) { return frame_.lensDestroyed; }
    const auto index=scene_index(spec.asset);
    if(index<std::size(kScenes)) {
        if(index==0 && spec.argument==0x417) { return escapeSpeechEnd_ && now_>=escapeSpeechEnd_; }
        if(spec.argument>=0x100 && spec.argument<0x100+49) { return sceneSpeechComplete_[index][spec.argument-0x100]; }
        if(spec.argument>=0x200 && spec.argument<0x240) { return sceneCueStarted_[index][spec.argument-0x200]; }
        if(spec.argument>=0x300 && spec.argument<0x340) { return sceneCueComplete_[index][spec.argument-0x300]; }
        return spec.argument==2 && sceneComplete_[index];
    }
    return entered(spec.asset,spec.argument==1);
}
coo::StallDetail Controller::missing(const coo::CommandSpec& spec) const noexcept {
    using coo::Missing;
    if(spec.wait==coo::Wait::requested || (coo::is_observation(spec.operation) && observed(spec))) { return {}; }
    if(spec.operation==coo::Operation::dialogue) { return {Missing::dialogue,spec.asset,spec.argument}; }
    if(spec.asset==kLens) { return {lens_.owner().valid()?Missing::death:Missing::object,spec.asset}; }
    if(spec.asset.type==43) { return {Missing::sceneBinding,spec.asset}; }
    return {Missing::observation,spec.asset};
}
void Controller::update_module(std::uint32_t id,const coo::MissionInput& input,Frame& output) noexcept {
    if(id!=1 || !views_ || input.run!=run_ || !arrived_) { return; }
    now_=input.now;frame_.enabled=true;
    frame_.gameplayClockTicks=clock_.sample(now_);
    if(!started_) { started_=executor_.start(graph()->definition,run_); }
    if(!started_) { return; }
    executor_.update(*this);
    for(const auto& binding:graph()->commands) {
        const auto state=executor_.step_state(binding.step);
        if(state.phase!=coo::StepPhase::active || !state.commands[binding.command].requested) { continue; }
        const auto& spec=graph()->definition.steps[binding.step].commands[binding.command];
        const auto token=executor_.token(binding.step,binding.command);
        if(coo::is_observation(spec.operation) && observed(spec)) { static_cast<void>(executor_.enqueue({token,coo::Milestone::observed})); }
        else if(spec.operation==coo::Operation::dialogue && spec.argument<49 && submitted_[spec.argument]) {
            static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady}));
        } else if(spec.operation==coo::Operation::scene) {
            const auto index=scene_index(spec.asset);
            if(index<std::size(kScenes) && scenes_[index].valid()) { static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady})); }
        }
    }
    executor_.update(*this);executor_.update(*this);
    dialogue_.advance(views_->dialogue,frame_.spawnGeneration-1U,now_,false,frame_,frame_.revision);
    if(executor_.diagnostics().phase==coo::Phase::complete && std::size_t(frame_.section)+1<views_->phases.size()) {
        ++frame_.section;executor_.cancel(*this);started_=executor_.start(graph()->definition,run_);
        if(started_) { executor_.update(*this); }
        ++frame_.revision;
    }
    frame_.enabled=executor_.diagnostics().phase!=coo::Phase::failed;
    // Charge duration remains the documented seven-second reconstruction estimate.
    if(!server::runtime::activity::mission_capture::update(frame_.plateCapture,frame_.plateRevision,
        frame_.native[asset_index(kPlate)].active && frame_.plateOccupied && !frame_.lensDestroyed,
        frame_.lensExposed && !frame_.lensDestroyed,coo::native_activity_ticks(7000),frame_.gameplayClockTicks)) {frame_.enabled=false;}
    frame_.plateCapture.presentationPosition=!frame_.lensDestroyed && (frame_.plateOccupied || frame_.lensExposed)?.1F:0.F;
        if(!server::runtime::activity::mission_device_pose::desire(frame_.plateCapture.pose,frame_.plateCapture.presentationPosition)) {frame_.enabled=false;}
    update_navigation();
    if(objectives_.state().active)objectives_.marker(navigation::marker(objectives_.state().event,frame_));
    frame_.checked=frame_.finished;frame_.presentation=objectives_.state();frame_.completion=lifecycle_.publication();output=frame_;
}
void Controller::update_navigation() noexcept {
    auto& p=frame_.navigation;
    if(frame_.forestPass==1 && entered({0x8E70632B,0x80F460EE,60,9},true))p.forestPastComplete=true;
    if(frame_.section>=3 && entered({0xC7FB7155,0x80F461BC,60,25},true))p.pastEncounter=true;
    if(frame_.section>=4 && p.pastEncounter && (entered({0xC7FB7155,0x80F461BC,60,24},true)
        || entered({0xC7FB7155,0x80F461BC,60,29},true) || entered({0x8E70632B,0x80F460EE,60,9},true)))p.pastReturn=true;
    if(frame_.forestPass==2 && entered({0x8E70632B,0x80F460EE,60,8},true))p.forestFutureComplete=true;
    if(frame_.section>=5 && entered({0x91AF0A4E,0x80F460DF,60,6},true))p.futureEntered=true;
    if(frame_.section>=5 && entered({0x15FFBE16,0x80F4608A,60,17},true))p.futureReflection=true;
    if(frame_.section>=6 && entered({0x45AFDE9B,0x80F460D4,60,3},true))p.futureReturn=true;
}
Frame Controller::update(std::uint64_t run,std::uint64_t now,bool ready) noexcept {
    if(!views_ || run!=run_ || !ready) { return {}; }
    if(!views_->observationStart) { arrived_=true; }
    return composition_.update(views_->mission,{run,now,0,false,true},*this);
}
}
