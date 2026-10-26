#include "server/runtime/activity/native_activity_profiles.h"
#include <cstdio>
#include <cstring>
namespace activity=dawn::server::runtime::activity;
namespace start=activity::adventure_start;
namespace native=dawn::middleware::bap::activity_message::native;
unsigned checks{};
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)

int main() {
    CHECK(!start::kLaunchesEnabled);
    unsigned routes{};
    for(const auto* profile:activity::kNativeActivityProfiles) {
        CHECK(profile);
        if(profile->openWorld) {
            unsigned npcs{},two{};
            for(const auto& p:profile->openWorld->authored->populations) {
                npcs+=p.kind==dawn::state::activity::coo::open_world::PopulationKind::npc;
                two+=p.categories==2;
            }
            std::printf("PROFILE %.*s ordinary=%zu lost=%zu npc=%u two=%u banners=%zu routes=%zu\n",
                int(profile->activity.size()),profile->activity.data(),profile->openWorld->authored->populations.size(),
                profile->lostSectors?profile->lostSectors->sources.size():0,npcs,two,profile->placements.size(),profile->startRoutes.size());
        }
        for(const auto& route:profile->startRoutes) {
            ++routes;
            start::Context context{};context.owner={0x4321,{2}};
            context.scenario=route.scenario;context.expectedRecordRevision=9;
            auto& current=context.current;current.hasAccount=current.hasNonce=true;
            current.account=11;current.nonce=22;current.revision=1;
            auto& selection=current.selection;selection.reason=1;
            selection.sourceActivityIndex=selection.activityIndex=route.activity;
            selection.hasPackageName=true;selection.packageNameLength=static_cast<std::uint8_t>(route.rootPackage.size());
            std::memcpy(selection.packageName.data(),route.rootPackage.data(),route.rootPackage.size());
            selection.descriptorBitLength=1;
            auto request=current;request.revision=2;
            context.published.count=1;
            context.published.entries[0]={route.registry,route.slot,route.bubble};
            start::Plan plan{};
            CHECK(start::prepare(context,request,profile->startRoutes,plan)==start::Result::unsupportedRoute);
            CHECK(!plan.owner);
            auto banners=context.published;
            banners.entries[banners.count++]={0x12345678,32700,63};
            const auto unrelated=banners.entries[1];
            start::restrict_banners(profile->startRoutes,banners);
            CHECK(!banners.entries[0].active && banners.entries[0].generation>0);
            CHECK(banners.entries[0].interactionMode==native::interaction::Mode::disabled);
            CHECK(banners.entries[1].registry==unrelated.registry && banners.entries[1].active==unrelated.active
                && banners.entries[1].generation==unrelated.generation);
        }
    }
    CHECK(routes==22);
    std::printf("PASS %u checks; all %u adventure routes reject launches and withdraw banners\n",checks,routes);
}
