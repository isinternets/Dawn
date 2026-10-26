#pragma once
#include "frame.h"
#include "../coo/native_activity_clock.h"
#include "../coo/object_service.h"
#include "../coo/stall_diagnostics.h"
namespace dawn::state::activity::beyond_infinity {
bool contains(const Volume&,Point) noexcept;
class Controller final : private coo::Services,private coo::MissionPorts<Frame> {
public:
    void reset() noexcept;
    bool select(const coo::script::Views&,std::uint64_t run) noexcept;
    void position(std::uint64_t run,Point) noexcept;
    bool prepared(coo::Generation,coo::Asset) noexcept;
    bool forest_ready(coo::Generation owner,std::uint8_t pass,bool ready) noexcept {
        if(owner!=this->owner() || pass!=frame_.forestPass || pass<1 || pass>2)return false;
        frame_.forestReady=ready;return true;
    }
    bool lens(const LensReceipt&,bool dead) noexcept;
    bool forest_terminal(coo::Generation owner,std::uint8_t pass) noexcept {
        if(owner!=this->owner() || pass!=frame_.forestPass || !frame_.enabled || frame_.finished || !frame_.forestReady)return false;
        if(pass==1 && frame_.section>=2 && frame_.section<=3) {frame_.navigation.forestPastComplete=true;return true;}
        if(pass==2 && frame_.section>=4 && frame_.section<=5) {frame_.navigation.forestFutureComplete=true;return true;}
        return false;
    }
    bool bind_plate(const PlateReceipt&) noexcept;
    bool plate_pose(const PlateReceipt&,server::runtime::activity::mission_device_pose::Sample) noexcept;
    bool plate(const PlateReceipt&,std::uint32_t revision,float value,bool complete) noexcept;
    bool scene(const SceneReceipt&,bool complete) noexcept;
    bool scene_speech(const SceneReceipt&,std::uint8_t row,std::uint32_t state) noexcept;
    bool transit(coo::Generation,std::uint8_t route) noexcept;
    bool scene_cue(const SceneReceipt&,std::uint8_t id,std::uint32_t state,std::uint32_t starts) noexcept;
    bool submitted(std::uint64_t run,std::uint32_t bank,std::uint8_t row,std::uint32_t generation,std::uint64_t now) noexcept;
    Frame update(std::uint64_t run,std::uint64_t now,bool ready) noexcept;
    const Frame& frame() const noexcept { return frame_; }
    LensReceipt lens_owner() const noexcept { return lens_.owner(); }
    PlateReceipt plate_owner() const noexcept { return plate_; }
    coo::Generation owner() const noexcept { return lifecycle_.owner(); }
    const coo::script::GraphView* graph() const noexcept { return views_ && frame_.section<views_->phases.size()?views_->phases[frame_.section]:nullptr; }
    coo::Diagnostics diagnostics() const noexcept { return executor_.diagnostics(); }
    auto step_state(std::size_t index) const noexcept { return executor_.step_state(index); }
    coo::StallDetail missing(const coo::CommandSpec&) const noexcept;
    const auto& seen() const noexcept { return seen_; }
private:
    void update_navigation() noexcept;
    bool publish(const coo::Command&) noexcept override;
    void cancel(const coo::Command&) noexcept override {}
    void update_module(std::uint32_t,const coo::MissionInput&,Frame&) noexcept override;
    std::uint32_t observations(std::uint64_t,const Frame& f) noexcept override { return f.checked?1U:0U; }
    bool observed(const coo::CommandSpec&) const noexcept;
    void request(coo::Asset,bool) noexcept;
    void well_channels() noexcept;
    bool entered(coo::Asset,bool current=false) const noexcept;
    const coo::script::Views* views_{};
    std::uint64_t run_{},now_{},escapeSpeechEnd_{};
    coo::NativeActivityClock clock_{};
    bool started_{},arrived_{};
    coo::LifecycleService lifecycle_{};
    coo::ObjectiveService objectives_{};
    coo::DialogueService<49> dialogue_{};
    coo::DestructibleService<LensReceipt> lens_{};
    PlateReceipt plate_{};
    std::uint32_t chargingRevision_{};
    std::array<SceneReceipt,std::size(kScenes)> scenes_{};
    std::bitset<std::size(kScenes)> sceneComplete_{};
    std::array<std::bitset<49>,std::size(kScenes)> sceneSpeechStarted_{},sceneSpeechComplete_{};
    std::array<std::bitset<64>,std::size(kScenes)> sceneCueStarted_{},sceneCueComplete_{};
    std::bitset<6> transitArrived_{};
    std::bitset<49> submitted_{};
    std::array<std::uint64_t,49> voiceEnd_{};
    std::bitset<std::size(kVolumes)> seen_{},inside_{};
    coo::MissionRuntime composition_{};
    coo::Executor executor_{};
    Frame frame_{};
};
}
