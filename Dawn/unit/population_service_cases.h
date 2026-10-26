#pragma once
#include "server/runtime/activity/mercury_populations.h"

void population_service_cases() {
    namespace p=dawn::server::runtime::activity::population;
    namespace m=dawn::server::runtime::activity::mercury;
    {
        std::array<p::Capability,1> capabilities{{m::kPopulations[0]}};
        capabilities[0].categories=2;
        const auto bubble=capabilities[0].registry->bubble;
        p::Service refill;CHECK(refill.begin({88,{3}},capabilities,123));
        p::Command delta{{88,{3}},1,1,capabilities[0].registry->key,capabilities[0].slot,1,123};
        CHECK(refill.replenish(delta,bubble)==p::Result::stale); // Never-started source.
        CHECK(refill.request(delta,bubble)==p::Result::accepted);
        delta.expectedRevision=refill.revision();delta.request=2;delta.requested=0;delta.secondRequested=1;
        CHECK(refill.replenish(delta,bubble)==p::Result::invalid); // Dormant category.
        delta.requested=3;CHECK(refill.request(delta,bubble)==p::Result::accepted);
        delta.expectedRevision=refill.revision();delta.request=3;delta.requested=0;
        CHECK(refill.replenish(delta,bubble+1)==p::Result::stale);
        auto wrong=delta;++wrong.boot;CHECK(refill.replenish(wrong,bubble)==p::Result::stale);
        wrong=delta;++wrong.owner.sessionId;CHECK(refill.replenish(wrong,bubble)==p::Result::stale);
        wrong=delta;++wrong.registry;CHECK(refill.replenish(wrong,bubble)==p::Result::unsupported);
        wrong=delta;wrong.requested=63;CHECK(refill.replenish(wrong,bubble)==p::Result::invalid);
        CHECK(refill.replenish(delta,bubble)==p::Result::accepted); // Second-only casualty.
        CHECK(refill.target(0)==3 && refill.second_target(0)==2 && refill.generation(0)==3);
        delta.expectedRevision=refill.revision();CHECK(refill.replenish(delta,bubble)==p::Result::duplicate);
        for(unsigned cycle=0;cycle<100;++cycle) {
            delta.expectedRevision=refill.revision();delta.request=refill.last_request()+1;
            delta.requested=3;delta.secondRequested=2;
            CHECK(refill.replenish(delta,bubble)==p::Result::accepted);
            CHECK(refill.generation(0)==3);
        }
        CHECK(refill.target(0)==303 && refill.second_target(0)==202);
        const auto projected=refill.project_retained().entries[0].source;
        CHECK(projected.looseRequested==303 && projected.secondRequested==202 && p::codec::valid(projected));
        dawn::middleware::bap::activity_message::sense_update::SenseObject mirror{};
        mirror.registryKey=capabilities[0].registry->key;mirror.slotType=1;mirror.slotIndex=capabilities[0].slot;
        mirror.hasNativeSchema=true;mirror.nativeSchema=0x80807ECC;mirror.nativeRevision=1;mirror.hasRootDelta=true;
        mirror.sourceDelta.present=1;mirror.sourceDelta.scalar[0]=3;
        mirror.sourceDelta.consumedPresent=true;mirror.sourceDelta.consumedCount=2;
        mirror.sourceDelta.consumed[0]=303;mirror.sourceDelta.consumed[1]=202;
        CHECK(refill.observe_retained(mirror) && refill.consumed(0));
        delta.expectedRevision=refill.revision();delta.request=refill.last_request()+1;
        CHECK(refill.renew(delta,bubble)==p::Result::accepted);
        CHECK(refill.renewal(0).target==303 && refill.renewal(0).secondTarget==202);
        ++delta.request;CHECK(refill.replenish(delta,bubble)==p::Result::exhausted);
        CHECK(refill.commit_renewal(0));CHECK(refill.target(0)==3 && refill.second_target(0)==2);
    }
    p::Service service;
    CHECK(service.begin({42,{7}},m::kPopulations));
    CHECK(!service.begin({42,{7}},m::kPopulations));
    CHECK(service.project(15).count==0);
    p::Command command{};
    CHECK(p::parse("v2 1 2A 7 1 1 74337EDD 0 1\n",command));
    p::Service restarted;CHECK(restarted.begin({42,{7}},m::kPopulations,2));
    CHECK(restarted.request(command,15)==p::Result::stale);
    CHECK(restarted.project(15).count==0);
    CHECK(!p::parse("v1 2A 7 1 1 74337EDD 0 1",command));
    CHECK(!p::parse("v2 0 2A 7 1 1 74337EDD 0 1",command));
    CHECK(!p::parse("v2 FFFFFFFFFFFFFFFFF 2A 7 1 1 74337EDD 0 1",command));
    CHECK(service.request(command,14)==p::Result::stale);
    CHECK(service.request(command,15)==p::Result::accepted);
    CHECK(service.revision()==2);
    auto batch=service.project(15);CHECK(batch.count==1);
    CHECK(batch.entries[0].source.looseRequested==1 && batch.entries[0].source.generation==7);
    CHECK(batch.entries[0].source.ruleSlot==9 && batch.entries[0].source.tactical.row==1);
    CHECK(service.project(14).count==0);
    dawn::middleware::bap::activity_message::sense_update::SenseObject sense{};
    sense.registryKey=0x74337EDD;sense.slotType=1;sense.slotIndex=0;
    sense.hasNativeSchema=true;sense.nativeSchema=0x80807ECC;
    sense.hasRootDelta=true;sense.nativeRevision=3;
    sense.sourceDelta.present=1;sense.sourceDelta.scalar[0]=6;
    CHECK(!service.observe(15,sense)); // old generation
    sense.sourceDelta.scalar[0]=7;
    CHECK(!service.observe(14,sense)); // other bubble
    const auto* observation=service.observe(15,sense);
    CHECK(observation && observation->known==1 && !observation->consumedKnown);
    CHECK(!service.observe(15,sense)); // duplicate packet on second binding
    sense.nativeRevision=4;sense.sourceDelta={};
    sense.sourceDelta.present=8;sense.sourceDelta.scalar[3]=1;
    observation=service.observe(15,sense);
    CHECK(observation && observation->scalar[0]==7 && observation->scalar[3]==1);
    sense.nativeRevision=3;CHECK(!service.observe(15,sense)); // reordered packet
    sense.nativeRevision=5;sense.sourceDelta={};
    sense.sourceDelta.consumedPresent=true;sense.sourceDelta.consumedCount=1;
    sense.sourceDelta.consumed[0]=1;
    observation=service.observe(15,sense);
    CHECK(observation && observation->consumedKnown && observation->scalar[3]==1);
    // A retained open-world source keeps reporting after the player crosses
    // into another bubble. Only the explicitly retained path accepts that
    // report, and it still requires a started exact source and generation.
    p::Service retained;CHECK(retained.begin({57,{20}},m::kPopulations,12));
    p::Command retainedCommand{{57,{20}},1,1,m::kPopulations[0].registry->key,
        m::kPopulations[0].slot,1,12};
    CHECK(retained.request(retainedCommand,m::kPopulations[0].registry->bubble)==p::Result::accepted);
    auto retainedSense=sense;retainedSense.registryKey=m::kPopulations[0].registry->key;
    retainedSense.slotIndex=m::kPopulations[0].slot;retainedSense.nativeRevision=1;
    retainedSense.sourceDelta={};retainedSense.sourceDelta.present=1;
    retainedSense.sourceDelta.scalar[0]=20;retainedSense.sourceDelta.consumedPresent=true;
    retainedSense.sourceDelta.consumedCount=1;retainedSense.sourceDelta.consumed[0]=1;
    CHECK(!retained.observe(m::kPopulations[0].registry->bubble+1,retainedSense));
    auto rejectedSense=retainedSense;rejectedSense.sourceDelta.scalar[0]=19;
    CHECK(!retained.observe_retained(rejectedSense)); // old generation
    rejectedSense=retainedSense;rejectedSense.registryKey^=1U;
    CHECK(!retained.observe_retained(rejectedSense)); // unsupported registry
    rejectedSense=retainedSense;++rejectedSense.slotIndex;
    CHECK(!retained.observe_retained(rejectedSense)); // wrong source slot
    rejectedSense=retainedSense;rejectedSense.registryKey=m::kPopulations[1].registry->key;
    rejectedSense.slotIndex=m::kPopulations[1].slot;
    CHECK(!retained.observe_retained(rejectedSense)); // exact but never started
    CHECK(retained.observe_retained(retainedSense) && retained.consumed(0));
    CHECK(!retained.observe_retained(retainedSense)); // duplicate native revision
    // A consumed request never synthesizes an associated-count decrement or kill.
    CHECK(service.request(command,15)==p::Result::stale);
    command.expectedRevision=2;
    CHECK(service.request(command,15)==p::Result::duplicate);
    command.request=2;command.requested=2;
    CHECK(service.request(command,15)==p::Result::accepted);
    command.expectedRevision=3;command.request=3;command.requested=1;
    CHECK(service.request(command,15)==p::Result::decrease);
    command.slot=17;CHECK(service.request(command,15)==p::Result::unsupported);
    command.registry=0x564C6ECE;command.slot=0;
    CHECK(service.request(command,15)==p::Result::accepted);
    batch=service.project(15);CHECK(batch.count==2);
    CHECK(!batch.entries[1].source.hasSpawnRule && batch.entries[1].source.tactical.row==-1);
    for(const auto text:{"v2 1 2A 7 1 1 74337EDD 0 0","v2 1 2A 7 1 1 74337EDD 0 64",
        "v2 1 2A 7 1 1 74337EDD 0 1 extra","v2 1 2A 7 1 1 74337EDD 0",
        "v2 1 0 7 1 1 74337EDD 0 1","v2 1 2A 0 1 1 74337EDD 0 1",
        "v2 1 2A 7 -1 1 74337EDD 0 1","v2 1 2A 7 1 1 FFFFFFFFF 0 1"})
        CHECK(!p::parse(text,command));
    CHECK(service.revision()==4 && service.last_request()==3);
    p::Service invalid;
    CHECK(!invalid.begin({0,{1}},m::kPopulations));
    CHECK(!invalid.begin({42,{0x80000000ULL}},m::kPopulations));
    auto malformed=m::kPopulations;malformed[0].rule=7;
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[0].tactical.slot=3;
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[1]=malformed[0];
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[1].taskMask=1; // Initial row one must be allowed.
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[1].taskMask|=1U<<24;
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[1].tactical.revision=0x80000000U;
    CHECK(!invalid.begin({43,{1}},malformed));
    malformed=m::kPopulations;malformed[2].tactical.revision=1; // No objective on a vendor.
    CHECK(!invalid.begin({43,{1}},malformed));

    // Only patrol sources opt in. A native cost report must not move a fixed
    // diagnostic, NPC, or public-event source, or select a row outside the mask.
    for(std::size_t i=0;i<m::kPopulations.size();++i)
        CHECK((m::kPopulations[i].taskMask!=0)==(i<=1 || i>=7));
    for(bool adaptive:{false,true}) {
        std::array<p::Capability,1> capabilities{{m::kPopulations[1]}};
        capabilities[0].taskMask=adaptive?3U:0U;
        p::Service patrol;CHECK(patrol.begin({54,{1}},capabilities,10));
        CHECK(patrol.request({{54,{1}},1,1,capabilities[0].registry->key,capabilities[0].slot,1,10},15)==p::Result::accepted);
        CHECK(patrol.tactical(0).row==1 && patrol.tactical(0).revision==(adaptive?1U:0U));
        sense={};sense.registryKey=capabilities[0].registry->key;sense.slotType=1;sense.slotIndex=capabilities[0].slot;
        sense.hasNativeSchema=true;sense.nativeSchema=0x80807ECC;sense.hasRootDelta=true;
        sense.nativeRevision=1;sense.sourceDelta.present=1;sense.sourceDelta.scalar[0]=1;
        sense.hasSquadOutput=true;sense.squadOutput.initialized=true;
        sense.squadOutput.hasRevision=true;sense.squadOutput.revision=1;
        sense.squadOutput.costMask=7;sense.squadOutput.cost[0]=10;
        sense.squadOutput.cost[1]=20;sense.squadOutput.cost[2]=0; // Cheapest but not permitted.
        CHECK(patrol.observe(15,sense));CHECK(patrol.tactical(0).row==(adaptive?0:1));
        CHECK(patrol.target(0)==1 && patrol.last_request()==1);
        CHECK(patrol.revision()==(adaptive?3U:2U));
    }

    // Two-category sources retain their authored wire width even while the
    // second category is explicitly dormant. Both category counters qualify
    // renewal, and the next generation preserves both requested targets.
    std::array<p::Capability,1> twoCapabilities{{m::kPopulations[0]}};
    twoCapabilities[0].categories=2;
    p::Service two;CHECK(two.begin({55,{3}},twoCapabilities,11));
    p::Command twoCommand{{55,{3}},1,1,twoCapabilities[0].registry->key,twoCapabilities[0].slot,1,11};
    CHECK(two.request(twoCommand,twoCapabilities[0].registry->bubble)==p::Result::accepted);
    auto twoWire=two.project(twoCapabilities[0].registry->bubble);
    CHECK(twoWire.count==1 && twoWire.entries[0].source.hasSecondCategory
        && twoWire.entries[0].source.looseRequested==1 && twoWire.entries[0].source.secondRequested==0);
    sense={};sense.registryKey=twoCapabilities[0].registry->key;sense.slotType=1;
    sense.slotIndex=twoCapabilities[0].slot;sense.hasNativeSchema=true;sense.nativeSchema=0x80807ECC;
    sense.hasRootDelta=true;sense.nativeRevision=1;sense.sourceDelta.present=1;sense.sourceDelta.scalar[0]=3;
    sense.sourceDelta.consumedPresent=true;sense.sourceDelta.consumedCount=1;sense.sourceDelta.consumed[0]=1;
    CHECK(two.observe(twoCapabilities[0].registry->bubble,sense) && !two.consumed(0));
    sense.nativeRevision=2;sense.sourceDelta.consumedCount=2;sense.sourceDelta.consumed[1]=0;
    CHECK(two.observe(twoCapabilities[0].registry->bubble,sense) && two.consumed(0));
    twoCommand.expectedRevision=two.revision();twoCommand.request=2;twoCommand.requested=2;twoCommand.secondRequested=3;
    CHECK(two.renew(twoCommand,twoCapabilities[0].registry->bubble)==p::Result::accepted);
    CHECK(two.renewal(0).target==1 && two.renewal(0).secondTarget==0
        && two.renewal(0).nextTarget==2 && two.renewal(0).nextSecondTarget==3
        && two.renewal(0).hasSecondCategory);
    CHECK(two.commit_renewal(0));twoWire=two.project_retained();
    CHECK(twoWire.entries[0].source.generation==4 && twoWire.entries[0].source.looseRequested==2
        && twoWire.entries[0].source.secondRequested==3 && two.second_target(0)==3);
    auto invalidTwo=twoCommand;invalidTwo.expectedRevision=two.revision();invalidTwo.request=3;
    invalidTwo.requested=63;invalidTwo.secondRequested=1;
    CHECK(two.request(invalidTwo,twoCapabilities[0].registry->bubble)==p::Result::invalid);
    std::array<p::Capability,1> oneCapability{{m::kPopulations[0]}};
    p::Service one;CHECK(one.begin({56,{3}},oneCapability,11));
    invalidTwo={{56,{3}},1,1,oneCapability[0].registry->key,oneCapability[0].slot,1,11,1};
    CHECK(one.request(invalidTwo,oneCapability[0].registry->bubble)==p::Result::invalid);

    // A recurring Mercury source changes generation only after its exact
    // consumed mirror. The next generation rejects the old mirror identity.
    p::Service recurring;CHECK(recurring.begin({52,{7}},m::kPopulations,9));
    p::Command recurringCommand{{52,{7}},1,1,0x74337EDD,0,1,9};
    CHECK(recurring.request(recurringCommand,15)==p::Result::accepted);
    sense={};sense.registryKey=0x74337EDD;sense.slotType=1;sense.slotIndex=0;
    sense.hasNativeSchema=true;sense.nativeSchema=0x80807ECC;sense.hasRootDelta=true;
    sense.nativeRevision=1;sense.sourceDelta.present=1;sense.sourceDelta.scalar[0]=7;
    sense.sourceDelta.consumedPresent=true;sense.sourceDelta.consumedCount=1;sense.sourceDelta.consumed[0]=1;
    CHECK(recurring.observe(15,sense) && recurring.consumed(0));
    recurringCommand.expectedRevision=2;recurringCommand.request=2;recurringCommand.requested=4;
    CHECK(recurring.renew(recurringCommand,15)==p::Result::accepted);
    auto requestWhileRenewing=recurringCommand;++requestWhileRenewing.request;requestWhileRenewing.requested=2;
    CHECK(recurring.request(requestWhileRenewing,15)==p::Result::exhausted);
    CHECK(recurring.project(15).entries[0].source.generation==7 && recurring.renewal(0).pending);
    CHECK(recurring.renewal(0).target==1 && recurring.renewal(0).nextTarget==4);
    CHECK(recurring.commit_renewal(0));
    CHECK(recurring.project(15).entries[0].source.generation==8
        && recurring.project(15).entries[0].source.looseRequested==4 && !recurring.renewal(0).pending);
    sense.nativeRevision=2;CHECK(!recurring.observe(15,sense));
    sense.sourceDelta.scalar[0]=8;sense.sourceDelta.consumed[0]=4;
    CHECK(recurring.observe(15,sense) && recurring.consumed(0));
    recurringCommand.expectedRevision=recurring.revision();
    recurringCommand.request=recurring.last_request()+1;recurringCommand.requested=3;
    CHECK(recurring.renew(recurringCommand,15)==p::Result::accepted);
    ++recurringCommand.request;
    CHECK(recurring.renew(recurringCommand,15)==p::Result::exhausted); // bridge acknowledgement still pending
    recurring.cancel_renewal(0);CHECK(!recurring.renewal(0).pending);
    for(std::uint32_t cycle=0;cycle<80;++cycle) {
        recurringCommand.expectedRevision=recurring.revision();
        recurringCommand.request=recurring.last_request()+1;
        recurringCommand.requested=static_cast<std::uint8_t>(3+cycle%2);
        CHECK(recurring.renew(recurringCommand,15)==p::Result::accepted);
        const auto generation=9+cycle;
        CHECK(recurring.project(15).entries[0].source.generation==generation-1);
        CHECK(recurring.commit_renewal(0));
        CHECK(recurring.project(15).entries[0].source.generation==generation
            && recurring.project(15).entries[0].source.looseRequested==recurringCommand.requested);
        sense.nativeRevision=3+cycle;sense.sourceDelta.scalar[0]=generation;
        sense.sourceDelta.consumed[0]=recurringCommand.requested;
        CHECK(recurring.observe(15,sense) && recurring.consumed(0));
    }
}
