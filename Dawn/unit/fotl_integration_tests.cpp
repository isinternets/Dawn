#include "../src/state/vendors/projection.h"
#include "../src/state/activity/Newlight/launchpad/quest.h"
#include "../src/state/account/festival_projection.h"
#include "../src/middleware/datagen/family4/character/layout.h"
#include "../src/middleware/web_service/messages/opcode904.h"
#include "../src/middleware/web_service/messages/opcode904/opcode904_codec.h"
#include <cstdio>
#include <cstdlib>
// These derived flags have no acquired-bank mapping in installed 81319322.
namespace dawn::state::build_data::vendors::services {
Binding binding(bool,std::uint16_t) noexcept {return {};}
}
namespace st=dawn::state;
namespace quest=st::activity::newlight::launchpad::quest;
namespace festival=st::account::festival_projection;
using Object=dawn::middleware::datagen::family4::character::layout::Object;
unsigned checks{};
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(false)
int flag(const Object& object,std::int16_t slot) {
    for(std::size_t i=0;i<object.unlockFlagCount;++i) if(object.unlockFlags[i].slot==slot) return object.unlockFlags[i].value;
    return -1;
}
int main() {
    for(const auto cls:{st::CharacterClass::hunter,st::CharacterClass::titan,st::CharacterClass::warlock})
    for(unsigned step=0;step<quest::kSteps.size();++step)
    for(const bool mask:{false,true}) {
        st::AccountState account{};auto& c=account.characters[0];c.soid=1;c.characterClass=cls;c.level=50;
        c.inventory.count=1;c.inventory.values[0].definitionHash=quest::kSteps[step];c.inventory.values[0].quantity=1;
        if(mask) {
            st::account::inventory::Item helmet{};helmet.instanceSoid=2;
            helmet.definitionHash=st::account::festival_mask::kDefinitionHashes.front();helmet.quantity=1;
            c.equipment.slots[static_cast<std::size_t>(st::account::inventory::EquipmentSlot::helmet)]=helmet;
        }
        Object object{};
        st::vendors::project_inventory(account,c,object);
        CHECK(object.unlockFlagCount==12);
        quest::project(c,object);CHECK(object.unlockFlagCount==16);
        CHECK(festival::project(c,true,object));CHECK(object.unlockFlagCount==20);
        CHECK(flag(object,20483)==2);CHECK(flag(object,20826)==2);
        CHECK(flag(object,st::account::festival_mask::kEquippedRequirementFlag)==(mask?2:0));
        CHECK(object.acquiredFlags[20]==(step==0?std::byte{2}:std::byte{}));
        CHECK(object.acquiredFlags[59]==(step==0?std::byte{}:std::byte{2}));
        CHECK(festival::project(c,false,object));CHECK(object.unlockFlagCount==20);
        CHECK(flag(object,20826)==0);CHECK(flag(object,20483)==2);
        for(std::size_t i=0;i<object.unlockFlagCount;++i)
            for(std::size_t j=i+1;j<object.unlockFlagCount;++j) CHECK(object.unlockFlags[i].slot!=object.unlockFlags[j].slot);
    }
    // Festival quest progression and the old vendor-reply codec can coexist.
    st::CharacterState c{};c.inventory.count=1;c.inventory.values[0].quantity=1;
    for(std::size_t i=0;i<st::account::festival_quest::kSteps.size();++i) {
        c.inventory.values[0].definitionHash=st::account::festival_quest::kSteps[i];
        Object object{};CHECK(festival::project(c,true,object));
        CHECK(flag(object,20826)==0);CHECK(flag(object,20829)==(i==0?2:0));
        CHECK(flag(object,20831)==(i==4?2:0));
    }
    std::printf("PASS: %u Festival/New Light/vendor shared flag-bank checks\n",checks);
}
