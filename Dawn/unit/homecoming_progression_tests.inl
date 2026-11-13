// Controller-level native-receipt simulation. This proves graph/lease behavior,
// not that the installed client rendered a performance or accepted a gun.
namespace {
namespace senses=dawn::middleware::bap::activity_message;
struct HomecomingRun {
    std::unique_ptr<m::Controller> c=std::make_unique<m::Controller>();
    std::uint64_t now{100},run{201};
    std::array<std::array<unsigned,32>,9> waits{};
    std::array<std::uint32_t,m::kSpawns.size()> dead{};
    std::bitset<9> sections{};
    std::bitset<std::size(m::kSpawnBatches)> batches{};
    bool caydeEntryReleased{},shaxxPrestage{},armoryWithoutDoorEnd{},postArmoryRadio{},postGunReleased{},postGunDamageReleased{};
    std::bitset<4> hallwayReleased{};
    bool hallwayIndependent{},reactorPreloaded{};
    bool reactorApproached{},reactorProximityOpened{};
    unsigned reactorApproachWaits{};
    std::uint64_t reactorApproachedAt{};
    bool postGunAreaEntered{},postGunAtDoor{},travelerDispatched{},plazaShipEntered{};
    std::uint32_t plazaShipGeneration{};
    std::array<std::uint32_t,2> reactorObjectGenerations{};
    std::uint32_t zavalaSceneGeneration{};
    std::uint32_t wardGeneration{};unsigned wardStarts{};
    std::array<std::uint64_t,3> waveClearedAt{};
    std::uint64_t shaxxSpeechAt{};
    unsigned grants{},cinemas{},lastAssault{},turbineTicks{};
    // Bazaar area: Ikora's blasted Cabal and one bazaar Cabal never die (stuck
    // behind the door or out of reach). The door and the Hawk follow the player.
    bool stuckArea{},ikoraSurvivors{},doorHeld{},doorOpened{},bazaarSurvivor{},landingHeld{},landed{};
    unsigned doorWait{},landingWait{};
    explicit HomecomingRun(std::uint64_t id):run(id) {check(c->select(run,now),"simulation selects a fresh mission");}
    void movie() {
        const auto s=c->frame().cinematic;const auto owner=c->owner();
        if(s.phase==m::cinematics::Phase::preparing) {check(c->arrival(owner,s.route(),now),"movie arrival receipt");}
        else if(s.phase==m::cinematics::Phase::offered) {
            check(c->cinematic(owner,{5239,m::kMovies[s.movie].registry,1,6,0},now),"movie start receipt");
        } else if(s.phase==m::cinematics::Phase::playing) {
            check(c->cinematic(owner,{1685,m::kMovies[s.movie].registry,2,6,0},now),"movie completion receipt");++cinemas;
        } else if(s.phase==m::cinematics::Phase::landing) {check(c->arrival(owner,s.route(),now),"gameplay landing receipt");}
    }
    void visit(coo::Asset a) {
        if(a==m::trigger_area(m::kUnderwatch,"pt_postgun")) postGunAreaEntered=true;
        for(const auto& v:m::kVolumes) if(v.asset==a) {
            m::Point p{};for(const auto& q:v.vertices) {p.x+=q.x;p.y+=q.y;}
            p.x/=static_cast<float>(v.vertices.size());p.y/=static_cast<float>(v.vertices.size());p.z=(v.min.z+v.max.z)*.5F;
            check(m::contains(v,p),"test route point lies inside its authored volume");c->position(run,p);return;
        }
        check(false,"test route resolves an authored volume");
    }
    void objects() {
        for(const auto& a:m::kObjects) {
            const auto i=m::asset_index(a.source);
            if(c->frame().native[i].managed && c->frame().native[i].desired) {
                static_cast<void>(c->prepared(c->owner(),a.source));
                const auto& n=c->frame().native[i];
                if(n.active && !n.observed) {
                    check(c->object({{run,n.generation},a.source,static_cast<std::uint32_t>(i+100),1}),"requested object binds on its create generation");
                }
            }
        }
    }
    void use(coo::Asset a) {
        const auto& n=c->frame().native[m::asset_index(a)];
        senses::object_sense::Output d{};d.generation=static_cast<std::int32_t>(n.generation);
        d.hasUse=d.used=d.present=d.alive=true;d.useRevision=1;
        auto stale=d;--stale.generation;
        check(!c->use(c->owner(),a,stale),"stale accepted-use generation is rejected");
        check(c->use(c->owner(),a,d),"live accepted-use generation is accepted");
        check(!c->use(c->owner(),a,d),"accepted interaction is consumed once");
    }
    void pose(coo::Asset a,float value) {
        if(a==m::asset(m::kShip,23,59)) {
            check(reactorApproached && now-reactorApproachedAt<=500,
                "reactor entrance opens promptly on approach without waiting for boss clearance");
            for(const auto member:m::kCohorts[static_cast<std::size_t>(m::Cohort::boss)].members) {
                const auto source=m::asset(member.registry,1,member.slot);
                const auto& state=c->frame().native[m::asset_index(source)];
                check(state.active && !state.sourceCleared && !dead[m::spawn_index(source)],
                    "opening the reactor entrance leaves both deck bosses alive and active");
            }
            reactorProximityOpened=true;
        }
        const auto i=m::asset_index(a);const auto owner=c->owner();
        if(!c->frame().native[i].deviceSynchronized) {
            senses::device_sense::Output initial{};initial.present=63;
            if(a.registry==m::kShip && (a.slot==59 || a.slot==60)) {
                initial.values[0]=value;initial.revisions.fill(1);
            }
            static_cast<void>(c->device(owner,a,initial));
        }
        senses::device_sense::Output d{};d.present=3;d.values[0]=value;
        d.revisions[0]=static_cast<std::int32_t>(c->frame().native[i].generation);
        if(!c->frame().native[i].acknowledged) {
            auto revisionOnly=d;revisionOnly.present=2;
            const auto& state=c->frame().native[i];
            const bool atTarget=state.poseKnown && std::abs(state.observedPosition-state.position)<.002F;
            static_cast<void>(c->device(owner,a,revisionOnly));
            check(c->frame().native[i].acknowledged==atTarget,
                "revision-only delta preserves real measured pose, never substitutes a target");
            if(a.registry==m::kShip && (a.slot==59 || a.slot==60)) {
                check(atTarget && c->frame().native[i].acknowledged,
                    "generator entry and escape doors accept unchanged open pose after revision bootstrap");
                const auto versions=c->frame().native[i].deviceVersions;
                auto stale=d;--stale.revisions[0];stale.present=11;stale.revisions[1]=100;stale.values[0]=0.F;
                check(!c->device(owner,a,stale) && c->frame().native[i].deviceVersions==versions
                    && c->frame().native[i].observedPosition==value,
                    "stale position revision rejects the whole delta before mutating cached channels");
            }
        }
        static_cast<void>(c->device(owner,a,d));
    }
    void clear(m::Cohort id) {
        // In the stuck-area run these enemies can never be killed.
        if(stuckArea && (id==m::Cohort::ikoraCabal || id==m::Cohort::bazaarAll)) {return;}
        for(const auto member:m::kCohorts[static_cast<std::size_t>(id)].members) {
            const auto a=m::asset(member.registry,1,member.slot);const auto i=m::spawn_index(a);
            const auto& n=c->frame().native[m::asset_index(a)];
            check(n.active,"clearance cannot require an unrequested source");
            if(a.registry==m::kShip && a.slot>=36 && a.slot<=55 && a.slot!=53 && !dead[i]) {
                const auto task=m::tactical(m::kSpawns[i]);
                check(task.registry==m::kShip && task.slot==1 && task.row==-1,
                    "interior and escape squads request the authored ship-wide objective cost pass");
                senses::source_sense::Output costs{};costs.present=2;costs.counters[1]=n.generation;
                costs.costMask=(1U<<12)|(1U<<14);costs.costs[12]=127;costs.costs[14]=3;
                auto stale=costs;--stale.counters[1];
                check(!c->source(c->owner(),a,stale),"foreign objective revision cannot assign combat tasks");
                check(c->source(c->owner(),a,costs) && c->frame().tactics[i].group==14,
                    "interior squad selects reachable authored row instead of an unreachable one");
                check(n.active && !n.sourceCleared && n.generation==costs.counters[1],
                    "combat assignment does not alter spawn generation or clearance");
            }
            if(dead[i]==n.generation) {continue;}
            for(unsigned j=0;j<m::expected(m::kSpawns[i]);++j) {
                m::EnemyReceipt r{run,static_cast<std::uint32_t>((i+1)*32+j),static_cast<std::uint32_t>(10000+i),n.generation,member.slot,member.registry};
                auto stale=r;--stale.generation;
                check(!c->admitted(stale),"old-generation actor cannot join this encounter");
                check(c->admitted(r),"every expected category choice admits one actor");
                check(c->died(r),"an admitted actor supplies a death receipt");
                check(!c->died(r),"duplicate death cannot advance assault clearance");
            }
            dead[i]=n.generation;
        }
        if(id==m::Cohort::wave1 || id==m::Cohort::wave2 || id==m::Cohort::wave3) {
            const unsigned wave=id==m::Cohort::wave1?0U:id==m::Cohort::wave2?1U:2U;
            if(!waveClearedAt[wave]) waveClearedAt[wave]=now;
        }
    }
    // Admit every expected actor of a cohort; only the first `deaths` of them die.
    void field(m::Cohort id,unsigned deaths) {
        unsigned n{};
        for(const auto member:m::kCohorts[static_cast<std::size_t>(id)].members) {
            const auto a=m::asset(member.registry,1,member.slot);const auto i=m::spawn_index(a);
            const auto& state=c->frame().native[m::asset_index(a)];
            check(state.active && !state.sourceCleared,"stuck-area sources are fielded and not yet cleared");
            for(unsigned j=0;j<m::expected(m::kSpawns[i]);++j,++n) {
                const m::EnemyReceipt r{run,static_cast<std::uint32_t>((i+1)*32+j),static_cast<std::uint32_t>(10000+i),state.generation,member.slot,member.registry};
                check(c->admitted(r),"stuck-area actor is admitted");
                if(n<deaths) check(c->died(r),"stuck-area actor dies");
            }
        }
        check(n>deaths,"at least one stuck-area actor stays alive");
    }
    void checkpoint_batches() {
        const auto& f=c->frame();
        for(const auto& a:m::kAssets) if(a.asset.type==23 && f.native[m::asset_index(a.asset)].managed) {
            check(!f.native[m::asset_index(a.asset)].snap,"ordinary mission device transitions must animate, including first activation");
        }
        for(std::size_t i=0;i<std::size(m::kSpawnBatches);++i) {
            check(!batches[i] || f.spawnCheckpoints[i],"a completed spawn checkpoint remains latched for this mission run");
            if(!f.spawnCheckpoints[i] || batches[i]) {continue;}
            batches.set(i);const auto& batch=m::kSpawnBatches[i];
            for(const auto group:batch.cohorts) for(const auto member:m::kCohorts[static_cast<std::size_t>(group)].members) {
                check(f.native[m::asset_index(m::asset(member.registry,1,member.slot))].active,
                    "a published checkpoint contains every requested cohort source");
            }
            for(const auto scene:batch.casts) for(const auto member:m::kScenes[m::scene_index(scene)].cast) if(member.type==1) {
                check(f.native[m::asset_index(m::asset(member.registry,member.type,member.slot))].active,
                    "a published checkpoint contains every reserved scene actor");
            }
        }
        const auto& cayde=f.scenes[m::scene_index(m::asset(m::kUnderwatch,43,17))];
        const auto ward=m::asset(m::kPlaza,43,4);
        const auto& wardNative=f.native[m::asset_index(ward)];
        if(f.section==static_cast<unsigned>(m::Section::plaza) || f.section==static_cast<unsigned>(m::Section::plazaWaves)) {
            check(!f.native[m::asset_index(m::asset(m::kPlaza,4,18))].desired,
                "Ward visuals belong to the native hands-out timeline, never an early duplicate shield object");
        }
        if(wardNative.active && wardNative.generation!=wardGeneration) {
            ++wardStarts;
            check(wardStarts==f.assaultsRepelled,"each Ward follows its own cleared assault");
            if(wardGeneration) {
                m::PlaybackReceipt stale{{c->owner(),ward,wardGeneration},100,1};stale.performanceFinished=true;
                check(!c->playback(stale,now),"first Ward completion cannot finish the second Ward");
                check(!f.scenes[m::scene_index(ward)].performanceFinished,"second Ward starts with a fresh completion latch");
            }
            wardGeneration=wardNative.generation;
        }
        if(cayde.count && !cayde.performanceFinished) {
            caydeEntryReleased=true;
            check(!f.native[m::asset_index(m::asset(m::kUnderwatch,4,81))].desired,
                "Cayde blocker retires when entry is sent, while his performance is still running");
        }
        const auto shaxx=m::asset(m::kUnderwatch,43,14);
        const auto& shaxxState=f.scenes[m::scene_index(shaxx)];
        if(cayde.count && !cayde.performanceFinished) {
            shaxxPrestage=true;
            check(f.spawnCheckpoints[static_cast<unsigned>(m::SpawnCheckpoint::shaxx)]
                && f.native[m::asset_index(shaxx)].active && f.native[m::asset_index(m::asset(m::kUnderwatch,1,15))].active,
                "Cayde entry activates Shaxx's native acquisition, not only a reserved population source");
            check(shaxxState.count==0 && shaxxState.speech.none(),"pre-staged Shaxx has no entry event or speech");
        }
        const auto postGun=m::asset(m::kUnderwatch,43,111);
        const auto& postGunState=f.scenes[m::scene_index(postGun)];
        if(!shaxxState.entryCue) {
            check(!f.native[m::asset_index(postGun)].active,
                "quiet Shaxx staging and entry submission alone cannot start the stairwell fight");
        } else if(f.native[m::asset_index(postGun)].active && !postGunAreaEntered) {
            postGunAtDoor=true;
            check(!f.generations[30],"early stairwell fight does not play the later evacuation announcement");
        }
        const auto& plazaShip=f.native[m::asset_index(m::asset(m::kPlazaProps,4,5))];
        if(!travelerDispatched) {
            check(!plazaShip.desired,"plaza arrival and a queued Ghost line do not start the plaza ship");
        } else if(plazaShip.prepared && plazaShip.active) {
            if(!plazaShipEntered) {plazaShipEntered=true;plazaShipGeneration=plazaShip.generation;}
            check(plazaShip.generation==plazaShipGeneration,"plaza ship entrance is not restarted by later objectives or waves");
        }
        if(postGunState.count) {
            check(postGunState.count<=2 && postGunState.events[0]==0x38857CF3U,
                "post-gun scene keeps its ordered opening release");
            if(postGunState.count==2) check(postGunReleased && postGunState.combatHeld && postGunState.events[1]==0x18EF2ABCU,
                "second-stage Cabal release follows the native opening receipt, exactly once");
            check(postGunState.started,"post-gun release waits for its current native selector");
            if(!postGunReleased) {
                m::PlaybackReceipt release{c->playback_request(postGun),postGunState.selector,postGunState.serial};
                release.combatReleased=3;
                auto stale=release;++stale.request.generation;
                check(!c->playback(stale,now),"stale post-gun release cannot change the frame or Cabal");
                for(const auto slot:{112,114}) {
                    check(!f.native[m::asset_index(m::asset(m::kUnderwatch,1,static_cast<std::uint16_t>(slot)))].sceneReleased,
                        "scene start and event submission alone do not fabricate a native flag-release receipt");
                }
                check(c->playback(release,now),"both authored flag removals are observed for the current cast");
                check(!c->playback(release,now),"repeated flag-removal observation is idempotent");
                for(const auto slot:{112,114}) {
                    const auto source=m::asset(m::kUnderwatch,1,static_cast<std::uint16_t>(slot));
                    const auto& state=f.native[m::asset_index(source)];
                    check(state.sceneReleased,"opening release maps to frame 112 and Cabal 114, not two Cabal");
                }
                check(!f.native[m::asset_index(m::asset(m::kUnderwatch,1,115))].sceneReleased,
                    "opening handoff does not fabricate a release for the third scene participant");
                check(postGunState.damageReleased==0,"opening flag removal is not proof of damage protection removal");
                postGunReleased=true;
            }
            if(postGunState.count==2 && !postGunDamageReleased) {
                m::PlaybackReceipt release{c->playback_request(postGun),postGunState.selector,postGunState.serial};
                release.damageReleased=2;
                auto stale=release;++stale.request.generation;
                check(!c->playback(stale,now),"stale second-stage removal cannot report Cabal damage readiness");
                check(c->playback(release,now) && !c->playback(release,now),"Cabal protection removal is independent and idempotent");
                check(postGunState.damageReleased==2,"parameter zero friendly frame is never fabricated as a released Cabal");
                const auto source=m::asset(m::kUnderwatch,1,114);const auto& state=f.native[m::asset_index(source)];
                const m::EnemyReceipt actor{run,600114,700000,state.generation,source.slot,source.registry};
                check(c->admitted(actor) && c->died(actor) && !f.fault,"one Cabal release retains one scene-owned admission and death");
                postGunDamageReleased=true;
            }
        }
        constexpr std::uint16_t hallwayScenes[]{70,72,74,77};
        for(std::size_t i=0;i<std::size(hallwayScenes);++i) {
            const auto scene=m::asset(m::kMilitary,43,hallwayScenes[i]);
            const auto& state=f.scenes[m::scene_index(scene)];
            if(!state.count) continue;
            check(state.started && state.combatHeld && state.count==1 && state.events[0]==0x18EF2ABCU,
                "each farther-hallway scene gets its authored combat release after native acquisition");
            hallwayReleased.set(i);
        }
        if(hallwayReleased[0] && hallwayReleased[1] && hallwayReleased[2] && !hallwayReleased[3]) hallwayIndependent=true;
        if(reactorPreloaded) for(std::size_t i=0;i<reactorObjectGenerations.size();++i) {
            const auto& state=f.native[m::asset_index(m::asset(m::kShip,4,static_cast<std::uint16_t>(148+i)))];
            check(state.desired && state.generation==reactorObjectGenerations[i],
                "chamber objective changes do not recreate or restart preloaded reactor props");
        }
        const auto zavala=m::asset(m::kPlaza,43,2);
        const auto generation=f.native[m::asset_index(zavala)].generation;
        if(f.native[m::asset_index(zavala)].active && generation!=zavalaSceneGeneration) {
            zavalaSceneGeneration=generation;
            const auto source=m::asset(m::kPlaza,1,6);
            const m::EnemyReceipt hero{run,500000+generation,600000,f.native[m::asset_index(source)].generation,6,m::kPlaza};
            check(!c->admitted(hero) && !c->frame().fault,"initial and revived Zavala actors are not enemy-ledger admissions or overflow");
            check(!c->died(hero) && !c->frame().native[m::asset_index(source)].sourceCleared,"Zavala deaths cannot retire his source as a cleared enemy");
        }
        check(!shaxxState.performanceFinished,"regression run never completes Shaxx's persistent door performance");
        if(shaxxState.count && !shaxxSpeechAt) {
            shaxxSpeechAt=now;
            m::PlaybackReceipt r{{c->owner(),shaxx,f.native[m::asset_index(shaxx)].generation},shaxxState.selector,shaxxState.serial};
            r.speech.set(22);
            check(c->playback(r,now),"Shaxx's actual armory speech is observed independently of his door child");
        }
        if(f.section==static_cast<unsigned>(m::Section::armory) && f.native[m::asset_index(m::kPickups[0])].desired) {
            armoryWithoutDoorEnd=true;
            check(f.native[m::asset_index(m::asset(m::kUnderwatch,23,117))].position==1.F,
                "armory rack and gun door are available while Shaxx door performance remains active");
        }
    }
    void observe(const coo::CommandSpec& cmd) {
        const auto a=cmd.asset;const auto arg=cmd.argument;
        if(cmd.operation==coo::Operation::eventAfter || a==m::kDialogueAsset) {return;}
        if(a==m::kModule) {
            if(arg>=0x100 && arg<0x100+std::size(m::kCohorts)) {
                check(arg!=0x100+static_cast<unsigned>(m::Cohort::boss),
                    "the mission must not wait for deck boss deaths to enter or leave the reactor");
                clear(static_cast<m::Cohort>(arg-0x100));return;
            }
            switch(static_cast<m::Milestone>(arg)) {
            case m::Milestone::caydeNear:visit(m::trigger_area(m::kUnderwatch,"pt_shaxx_enters"));return;
            case m::Milestone::shaxxNear:visit(m::trigger_area(m::kUnderwatch,"pt_start_shaxx_scene"));return;
            case m::Milestone::weaponGranted: {
                if(!c->frame().native[m::asset_index(m::kPickups[0])].observed) {return;}
                if(!c->frame().weaponUsed) {use(m::kPickups[0]);}
                const auto request=c->grant_request();check(request.valid(),"accepted rack hold authorizes delivery");
                check(!c->granted(request,0),"a missing inventory instance cannot finish delivery");
                check(c->granted(request,9000+request.reward),"confirmed equipment acknowledges one reward");++grants;
                check(!c->granted(request,9000+request.reward),"same inventory receipt cannot acknowledge both rewards");
                check(c->frame().weaponGranted==(grants==2),"armory waits for both starter weapons");return;
            }
            case m::Milestone::consoleScanned: {
                const auto generation=static_cast<std::int32_t>(c->frame().spawnGeneration+1);
                check(!c->ghost(c->owner(),m::kConsoleLink,{false,1.F,generation}),"inactive scan without prior progress is not completion");
                check(c->ghost(c->owner(),m::kConsoleLink,{true,.5F,generation}),"scan reports actual progress");
                check(c->ghost(c->owner(),m::kConsoleLink,{false,1.F,generation}),"scan completes on matching inactive receipt");return;
            }
            case m::Milestone::bazaarDoor: {
                if(!stuckArea) {clear(m::Cohort::ikoraCabal);return;}
                if(!ikoraSurvivors) {field(m::Cohort::ikoraCabal,3);ikoraSurvivors=true;}
                check(!c->frame().native[m::asset_index(m::kBazaarDoor)].managed,
                    "the bazaar door stays shut until Ikora's Cabal die or the player reaches it");
                if(++doorWait<200) return;
                doorHeld=true;visit(m::trigger_area(m::kBoulevard,"pt_bazaar"));return;
            }
            case m::Milestone::bazaarCrossed: {
                if(!stuckArea) {clear(m::Cohort::bazaarAll);return;}
                if(!bazaarSurvivor) {
                    unsigned total{};
                    for(const auto member:m::kBazaarAll) total+=m::expected(m::kSpawns[m::spawn_index(m::asset(member.registry,1,member.slot))]);
                    field(m::Cohort::bazaarAll,total-1);bazaarSurvivor=true;
                }
                check(!c->frame().native[m::asset_index(m::asset(m::kBoulevard,4,12))].desired,
                    "Holliday does not land while a bazaar Cabal lives and the player is short of the far end");
                if(++landingWait<200) return;
                landingHeld=true;visit(m::trigger_area(m::kBoulevard,"pt_bazaar_2"));return;
            }
            default:return; // Turbines are supplied in C/A/B order below.
            }
        }
        if(a.type==43) {
            check(a!=m::asset(m::kUnderwatch,43,14) || arg!=static_cast<unsigned>(m::SceneEvent::performanceFinished),
                "mission never waits for Shaxx door child completion");
            const auto generation=c->frame().native[m::asset_index(a)].generation;
            if(!c->frame().scenes[m::scene_index(a)].started) {
                senses::scene_sense::Output echo{};echo.delta=echo.hasSourceRevision=true;
                echo.generationWire=0x80000000U+generation;echo.sourceRevision=c->frame().scenes[m::scene_index(a)].revision;
                static_cast<void>(c->scene(c->owner(),a,echo));
                check(!c->frame().scenes[m::scene_index(a)].started,"request acknowledgement is not scene playback");
            }
            m::PlaybackReceipt r{c->playback_request(a),static_cast<std::uint32_t>(m::scene_index(a)+1000),generation};
            const auto& receiptState=c->frame().scenes[m::scene_index(a)];
            const bool outstanding=[&] {
                switch(static_cast<m::SceneEvent>(arg)) {
                case m::SceneEvent::started:return !receiptState.started;
                case m::SceneEvent::performanceFinished:return !receiptState.performanceFinished;
                case m::SceneEvent::entryCue:return !receiptState.entryCue;
                case m::SceneEvent::combatHeld:return !receiptState.combatHeld;
                case m::SceneEvent::combatOpeningReleased:return !(receiptState.combatReleased&2U);
                case m::SceneEvent::combatDamageReleased:return !(receiptState.damageReleased&2U);
                default:return false; // completion/applied-input receipts use the sense adapter
                }
            }();
            if(outstanding)
                check(m::playback_pending(r.request,receiptState),
                    "observer retirement never hides an outstanding mission scene wait");
            if(a==m::kZavalaRevival.scene) {
                check(r.request.revival==c->frame().reviveUsed,"Zavala completion branch follows the accepted revive, not the generation number");
                auto wrongBranch=r;wrongBranch.request.revival=!r.request.revival;wrongBranch.performanceFinished=true;
                check(!c->playback(wrongBranch,now),"arrival completion cannot answer revival, or vice versa, even on the same selector lease");
            }
            r.performanceFinished=arg==static_cast<unsigned>(m::SceneEvent::performanceFinished);
            r.entryCue=arg==static_cast<unsigned>(m::SceneEvent::entryCue);
            r.combatHeld=arg==static_cast<unsigned>(m::SceneEvent::combatHeld);
            r.combatReleased=a==m::asset(m::kUnderwatch,43,111)?0:UINT16_MAX;
            auto stale=r;++stale.request.generation;
            check(!c->playback(stale,now),"foreign scene generation cannot release a transition");
            static_cast<void>(c->playback(r,now));
            for(const auto& ref:m::kScenes[m::scene_index(a)].cast) {
                const auto source=m::asset(ref.registry,ref.type,ref.slot);const auto spawn=m::spawn_index(source);
                if(r.combatReleased && spawn<m::kSpawns.size() && m::tactical(m::kSpawns[spawn]).registry) {
                    check(c->frame().native[m::asset_index(source)].sceneReleased,"release resolves untyped cast references to their exact catalog assets");
                }
            }
            auto recycled=r;++recycled.serial;
            check(!c->playback(recycled,now),"recycled selector cannot finish a performance");
            if(arg==static_cast<unsigned>(m::SceneEvent::completed)) {
                senses::scene_sense::Output d{};d.delta=d.completed=d.hasSourceRevision=true;
                d.generationWire=0x80000000U+generation;d.sourceRevision=c->frame().scenes[m::scene_index(a)].revision;
                check(c->scene(c->owner(),a,d),"native root completion opens the death-scene gate");
            }
        } else if(a.type==23) {pose(a,std::bit_cast<float>(arg));}
        else if(a.type==4) {if(arg==static_cast<unsigned>(m::ObjectEvent::used)) {use(a);}}
        else {
            if(a==m::trigger_area(m::kShip,"pt_engine_room_lower")) {
                check(!c->frame().native[m::asset_index(m::asset(m::kShip,23,59))].managed,
                    "reactor entrance stays closed while the player has not approached");
                if(++reactorApproachWaits<8) return;
                for(const auto member:m::kCohorts[static_cast<std::size_t>(m::Cohort::boss)].members) {
                    const auto source=m::asset(member.registry,1,member.slot);
                    const auto i=m::spawn_index(source);
                    const auto& state=c->frame().native[m::asset_index(source)];
                    check(state.active && !state.sourceCleared,"deck bosses are spawned before reactor approach");
                    for(unsigned j=0;j<m::expected(m::kSpawns[i]);++j) {
                        const m::EnemyReceipt actor{run,static_cast<std::uint32_t>((i+1)*32+j),
                            static_cast<std::uint32_t>(10000+i),state.generation,member.slot,member.registry};
                        check(c->admitted(actor),"reactor approach test admits living bosses without reporting any deaths");
                    }
                }
                reactorApproached=true;reactorApproachedAt=now;
            }
            if(a==m::trigger_area(m::kUnderwatch,"pt_postgun") && !postGunAtDoor) {
                return; // Hold the player back until the door beat has already started the fight.
            }
            if(stuckArea && !doorHeld && (a==m::trigger_area(m::kBoulevard,"pt_bazaar") || a==m::volume_named(m::kBoulevardRoute,"tv_bazaar"))) {
                return; // Hold the player short of the bazaar door while Ikora's Cabal live.
            }
            if(a==m::trigger_area(m::kShipRoute,"pt_destroy_battleship")) {
                const auto& f=c->frame();
                check(f.presentation.event!=m::kObjectives[11],"reactor visual preload does not publish the chamber objective");
                for(std::size_t i=0;i<reactorObjectGenerations.size();++i) {
                    const auto& state=f.native[m::asset_index(m::asset(m::kShip,4,static_cast<std::uint16_t>(148+i)))];
                    check(state.managed && state.active && state.desired,"reactor props are already loaded before chamber entry");
                    reactorObjectGenerations[i]=state.generation;
                }
                for(const auto slot:{61,62,63,64,65,66,67,159,160,161,162}) {
                    const auto& state=f.native[m::asset_index(m::asset(m::kShip,23,static_cast<std::uint16_t>(slot)))];
                    check(state.managed && state.active && state.position==(slot==61 || slot==62 || slot==64?.1F:1.F),
                        "reactor beam, assembly and lights activate from the preceding room");
                }
                for(const auto slot:{143,145,147}) check(!f.native[m::asset_index(m::asset(m::kShip,23,static_cast<std::uint16_t>(slot)))].managed,
                    "visual preloading does not arm turbine damage before chamber entry");
                for(const auto slot:{142,144,146}) check(!f.native[m::asset_index(m::asset(m::kShip,4,static_cast<std::uint16_t>(slot)))].desired,
                    "shootable turbine objects remain gated by chamber entry");
                reactorPreloaded=true;
            }
            visit(a);
        }
    }
    void play() {
        std::string error;
        auto document=coo::script::MissionDocument::read(std::filesystem::path(__FILE__).parent_path().parent_path()/"scripts/homecoming.lua",m::kEntryProfile,error);
        m::Entry entry;
        check(document && entry.select(document->views(),*c,run,now),"full route uses the shipped Lua composition");
        for(unsigned tick=0;tick<8000 && !c->frame().finished;++tick) {
            check(!m::continuation::ready(c->owner(),c->frame()),"gameplay and unfinished movies cannot queue Exodus");
            now+=250;movie();check(entry.update(*c,run,now,true).enabled,"simulation advances the selected run");
            const auto& f=c->frame();check(f.enabled && !f.fault,"route has no rejected commands");sections.set(f.section);
            checkpoint_batches();
            if(stuckArea && f.section==static_cast<unsigned>(m::Section::boulevard)) {
                if(!doorOpened && f.native[m::asset_index(m::kBazaarDoor)].managed) {
                    doorOpened=true;
                    check(doorHeld && ikoraSurvivors,"the bazaar door opens on the player's arrival, not on an Ikora Cabal kill count");
                    check(!f.native[m::asset_index(m::asset(m::kBoulevard,1,22))].sourceCleared,
                        "Ikora's fourth blasted Cabal is still alive when the door opens");
                }
                if(!landed && f.native[m::asset_index(m::asset(m::kBoulevard,4,12))].desired) {
                    landed=true;
                    check(landingHeld && bazaarSurvivor,"Holliday lands when the player reaches the far end, with a bazaar Cabal still alive");
                }
            }
            if(f.assaultsRepelled!=lastAssault) {
                check(f.assaultsRepelled==lastAssault+1,"assault counter advances one completed wave at a time");lastAssault=f.assaultsRepelled;
                check(waveClearedAt[lastAssault-1] && now-waveClearedAt[lastAssault-1]<=m::kSettleMs+500,
                    "verified wave clearance updates the counter without dialogue/intermission padding");
            }
            if(f.assaultsRepelled<2) {
                check(!f.native[m::asset_index(m::asset(m::kPlaza,1,36))].active,"wave three stays dormant before two completed assaults");
            }
            if(f.assaultsRepelled<1) {
                check(!f.native[m::asset_index(m::asset(m::kPlaza,1,31))].active,"wave two stays dormant before the first completed assault");
            }
            if(f.cinematic.phase!=m::cinematics::Phase::gameplay) {continue;}
            if(f.activeRow!=coo::kNoDialogue) {
                const auto row=f.activeRow;
                if(row==34) {
                    check(f.native[m::asset_index(m::asset(m::kMilitary,4,0))].observed,
                        "command-ship sighting cannot play before native ship presence");
                }
                if(row==28) {
                    postArmoryRadio=true;
                    check(shaxxSpeechAt && now>=shaxxSpeechAt+m::kDialogue[22].durationMs+m::kDialoguePolicy.spacingMs,
                        "radio resumes after Shaxx speech ends, without waiting for his looping door");
                }
                if(row==40) {
                    const coo::CommandSpec start{coo::Operation::observation,m::kDialogueAsset,m::kDialogueStarted|40U,coo::Wait::observed};
                    const coo::CommandSpec end{coo::Operation::observation,m::kDialogueAsset,40U,coo::Wait::observed};
                    check(c->missing(start).missing!=coo::Missing::none,"queued Traveler line is not proof of playback start");
                    check(!c->submitted(run,m::kBank,row,f.generations[row]+1,now)
                        && c->missing(start).missing!=coo::Missing::none,"stale dialogue dispatch cannot launch the ship");
                    check(c->submitted(run,m::kBank,row,f.generations[row],now),"current Traveler dispatch is accepted");
                    travelerDispatched=true;
                    check(c->missing(start).missing==coo::Missing::none && c->missing(end).missing!=coo::Missing::none,
                        "ship cue observes voice START, while existing spoken gates still wait for the authored end");
                    check(!c->submitted(run,m::kBank,row,f.generations[row],now),"duplicate dispatch cannot replay the entrance");
                } else {
                    check(c->submitted(run,m::kBank,row,f.generations[row],now),"native dispatch acknowledges mission dialogue");
                }
            }
            objects();
            if(f.section==static_cast<unsigned>(m::Section::generator) && f.native[m::asset_index(m::asset(m::kShip,23,143))].managed) {
                ++turbineTicks;
                constexpr unsigned order[]{2,0,1};
                for(unsigned j=0;j<3;++j) if(turbineTicks==1+j*16) {
                    const auto a=m::asset(m::kShip,23,static_cast<std::uint16_t>(143+2*order[j]));
                    pose(a,.1F);
                    senses::device_sense::Output death{};death.present=3;death.values[0]=.4F;
                    death.revisions[0]=static_cast<std::int32_t>(f.native[m::asset_index(a)].generation)-1;
                    check(!c->device(c->owner(),a,death) && !f.generatorDown[order[j]],
                        "pre-activation terminal turbine pose cannot count");
                    death.revisions[0]+=3;
                    check(c->device(c->owner(),a,death),"native destruction may advance revision beyond setup");
                    check(!c->device(c->owner(),a,death),"duplicate destruction does not increment the counter");
                    check(f.generatorDown[order[j]],"turbine destruction accepts non-alphabetical order");
                }
            }
            const auto& g=c->graph().definition;
            for(std::size_t i=0;i<g.steps.size();++i) {
                const auto state=c->step_state(i);if(state.phase!=coo::StepPhase::active) {continue;}
                auto& age=waits[f.section][i];++age;
                for(const auto& cmd:g.steps[i].commands) {
                    if(!coo::is_observation(cmd.operation) || c->missing(cmd).missing==coo::Missing::none) {continue;}
                    // Deliberately wait thirty seconds for real scene/clearance receipts.
                    // No fixed five/ten-second timer is allowed to skip these gates.
                    if((cmd.asset.type==43 || (cmd.asset==m::kModule && cmd.argument>=0x100 && cmd.argument<0x200)) && age<120) {continue;}
                    if(cmd.asset==m::asset(m::kMilitary,43,77) && cmd.argument==static_cast<unsigned>(m::SceneEvent::combatHeld) && age<160) continue;
                    observe(cmd);
                }
            }
        }
        if(!c->frame().finished) {
            std::fprintf(stderr,"route stalled section=%u\n",c->frame().section);
            for(std::size_t i=0;i<c->graph().definition.steps.size();++i) if(c->step_state(i).phase==coo::StepPhase::active) {
                const auto& s=c->graph().definition.steps[i];std::fprintf(stderr,"waiting: %.*s\n",static_cast<int>(s.name.size()),s.name.data());
            }
        }
        check(c->frame().finished && sections.all(),"native-receipt simulation crosses all nine sections and completes the outro");
        check(postGunDamageReleased && hallwayReleased.all(),"both post-armory and farther-hallway combat handoffs are delivered");
        check(hallwayIndependent && reactorPreloaded,"independent hallway releases and early reactor preload are exercised");
        check(reactorApproached && reactorApproachWaits==8 && reactorProximityOpened,
            "full mission completes after a proximity-only reactor entrance with both bosses left alive");
        check(postGunAtDoor && postGunAreaEntered && travelerDispatched && plazaShipEntered,
            "stairwell fight starts at Shaxx's door and plaza ship starts at the preceding Ghost line");
        check(grants==2 && cinemas==3 && lastAssault==3,"full route supplies two weapons, three cinematics and three completed assaults");
        check(wardStarts==2,"full route performs both inter-wave shields on separate native generations");
        for(std::size_t i=0;i<batches.size();++i) {if(!batches[i]) std::fprintf(stderr,"missing checkpoint %zu\n",i);}
        check(batches.all(),"full route crosses every encounter spawn checkpoint");
        check(caydeEntryReleased && shaxxPrestage && armoryWithoutDoorEnd && postArmoryRadio && postGunReleased && zavalaSceneGeneration==2,
            "full route proves early silent Shaxx, usable armory, both post-gun releases and subsequent radio");
        check(c->frame().activeRow==coo::kNoDialogue,"outro has no late queued dialogue");
        check(!c->grant_request().valid(),"completed mission cannot issue inventory rewards");
        wire(c->frame());
        const auto owner=c->owner();
        check(m::continuation::ready(owner,c->frame()),"only completed outro authorizes Exodus");
        for(unsigned tick=0;tick<30;++tick) {
            now+=1000;const auto terminal=entry.update(*c,run,now,true);
            check(terminal.enabled && terminal.finished && terminal.completion.owner==owner
                && terminal.completion.state==6,"terminal authority survives beyond the host's 20-second timeout");
            const auto lifetime=m::continuation::lifetime(terminal,3);
            using dawn::middleware::bap::activity_message::sensor_auth_update::kLifetimeStates;
            check(lifetime==6 && std::find(kLifetimeStates.begin(),kLifetimeStates.end(),lifetime)!=kLifetimeStates.end(),
                "terminal host lifetime remains encodable instead of rejected state 8");
            check(m::continuation::publication_interval(terminal)==1000,"finished host keeps a bounded one-second publication cadence");
            wire(terminal);
        }
        auto terminal=std::make_unique<m::Frame>(c->frame());
        check(!m::continuation::ready({owner.run+1,owner.value},*terminal),"old run cannot queue a continuation");
        check(!m::continuation::ready({owner.run,owner.value+1},*terminal),"old generation cannot queue a continuation");
        terminal->fault=true;check(!m::continuation::ready(owner,*terminal),"faulted mission cannot queue Exodus");terminal->fault=false;
        terminal->cinematic.movie=m::cinematics::kPickup;
        check(!m::continuation::ready(owner,*terminal),"pickup completion cannot queue Exodus");terminal->cinematic.movie=m::cinematics::kOutro;
        for(const auto phase:{m::cinematics::Phase::offered,m::cinematics::Phase::playing,m::cinematics::Phase::stopping,m::cinematics::Phase::failed}) {
            terminal->cinematic.phase=phase;
            check(!m::continuation::ready(owner,*terminal),"unstarted, playing, skipped-but-not-stopped and failed outro cannot queue Exodus");
        }
        check(!entry.update(*c,run+1,now,true).enabled && !entry.update(*c,run,now,false).enabled,
            "terminal retention cannot publish for a foreign run or unready host");
    }
};
static void progression() {
    {
        std::size_t watched{};
        for(const auto& scene:m::kScenes) {
            bool expected{};
            for(const auto definition:m::kSceneActionDefinitions) expected|=definition==scene.asset.definition;
            for(const auto& phase:m::mission().phases) for(const auto& step:phase.definition.steps)
                for(const auto& command:step.commands)
                    expected|=command.operation==coo::Operation::observation && command.asset==scene.asset;
            const auto* selected=m::playback_scene(scene.asset.definition);
            check((selected!=nullptr)==expected,"scene observer inspects exactly the consumed progression and native-action receipts");
            if(selected) {check(*selected==scene.asset,"watched scene retains exact registry/slot identity");++watched;}
        }
        check(watched>0 && watched<std::size(m::kScenes),"decorative scenes do not acquire a playback observer");
        check(!m::playback_scene(0) && !m::playback_scene(UINT32_MAX),"unknown scene descriptors cannot be observed");
        for(const auto& performance:m::kPerformances)
            check(m::playback_scene(performance.scene.definition)!=nullptr,"all completion performances remain observed");
        for(const auto& hold:m::kCombatHolds)
            check(m::playback_scene(hold.scene.definition)!=nullptr,"all Cabal release holds remain observed");
        check(m::playback_scene(m::kShaxxDoorOpening.scene.definition)!=nullptr,"Shaxx door-start cue remains observed");
        for(const auto& plan:m::kSceneActionPlans) {
            check(m::scene_action_plan(plan.graph,plan.root)==&plan,"action plan requires the exact graph and root");
            check(!m::scene_action_plan(plan.graph,plan.root+1),"changed graph layout fails closed");
            for(const auto& action:plan.actions) {
                check(action.node!=0 && action.definition!=0,"action watch pins both runtime and definition offsets");
                check(action.value<(action.kind==m::SceneActionKind::speech?std::size(m::kDialogue):16),
                    "action bit and speech indices are bounded");
            }
        }
        check(!m::scene_action_plan(0,0),"unknown graphs cannot produce watched action receipts");
        std::printf("Scene observer: %zu of %zu catalogued scenes require inspection\n",watched,std::size(m::kScenes));
    }
    {
        HomecomingRun sparse(203);const auto a=m::asset(m::kShip,23,59);const auto owner=sparse.c->owner();
        const auto index=m::asset_index(a);
        senses::device_sense::Output d{};d.present=1;d.values[0]=1.F;
        static_cast<void>(sparse.c->device(owner,a,d));
        check(sparse.c->frame().native[index].poseKnown && sparse.c->frame().native[index].observedRevision==-1
            && !sparse.c->frame().native[index].acknowledged,"value-before-revision caches measurement without acknowledging anything");
        d.present=2;d.revisions[0]=1;static_cast<void>(sparse.c->device(owner,a,d));
        check(sparse.c->frame().native[index].poseKnown && sparse.c->frame().native[index].observedPosition==1.F,
            "independent revision delta retains a previously measured value");
        check(sparse.c->select(204,100),"new run resets sparse device caches");
        check(!sparse.c->device(owner,a,d) && !sparse.c->frame().native[index].poseKnown,
            "previous run cannot repopulate a reset device cache");
        static_cast<void>(sparse.c->device(sparse.c->owner(),a,d));
        check(!sparse.c->frame().native[index].poseKnown,"revision-only first receipt cannot invent a pose");
    }
    check(m::natural_performance(2,0) && !m::natural_performance(2,1) && !m::natural_performance(1,0),"cancelled/playing child is not a natural scene finish");
    for(std::uint8_t state=0;state<4;++state) for(std::int32_t stops=-1;stops<3;++stops) {
        check(m::door_opening_started(state,stops)==(state==1 && stops==0),
            "Shaxx entry cue requires the running, uncancelled door-opening child");
    }
    check(!m::performance_for({{201,1},m::kShaxxDoorOpening.scene,1,false}),
        "door entry observation does not make Shaxx's persistent child a dialogue or completion gate");
    const m::PlaybackRequest arrival{{201,1},m::kZavalaRevival.scene,77,false},revival{{201,1},m::kZavalaRevival.scene,77,true};
    check(m::performance_for(arrival) && m::performance_for(arrival)->node==0x1CC0,
        "initial arrival uses the original flag-removal handoff regardless of generation");
    check(m::performance_for(revival)==&m::kZavalaRevival && m::performance_for(revival)->node==0x6670,
        "revival watches its own completed animation, not the dormant arrival action");
    for(std::uint8_t animation=0;animation<3;++animation) for(std::uint8_t idle=0;idle<3;++idle) {
        check(m::revival_handoff(animation,idle)==(animation==2 && idle==1),
            "revival requires completed animation followed by active authored idle");
    }
    HomecomingRun run(201);run.play();
    // The bazaar area must finish even when its enemies never die: the door opens
    // when the player reaches it and Holliday lands at the bazaar's far end.
    HomecomingRun stuck(206);stuck.stuckArea=true;stuck.play();
    check(stuck.ikoraSurvivors && stuck.doorHeld && stuck.doorOpened && stuck.bazaarSurvivor && stuck.landingHeld && stuck.landed,
        "a bazaar area whose enemies cannot be killed still opens the door and brings Holliday");
}
static void centurion_spawn_regression() {
    HomecomingRun run(202);
    for(unsigned tick=0;tick<8 && run.c->frame().cinematic.phase!=m::cinematics::Phase::gameplay;++tick) {
        run.now+=250;run.movie();check(run.c->advance(run.run,run.now,true),"regression reaches the opening");
    }
    check(run.c->frame().cinematic.phase==m::cinematics::Phase::gameplay,"regression lands in Underwatch");
    run.visit(m::trigger_area(m::kUnderwatch,"pt_centurion_intro"));
    check(run.c->advance(run.run,++run.now,true),"centurion trigger stages its authored scene");
    const auto source=m::asset(m::kUnderwatch,1,20),member=m::asset(m::kUnderwatch,2,21);
    check(run.c->frame().native[m::asset_index(source)].active && run.c->frame().native[m::asset_index(member)].bound,
        "Centurion source and named member are published together");
    std::array<std::byte,2048> storage{};
    dawn::middleware::encoding::bits::Writer writer(storage);
    check(m::write_body(writer,run.c->frame(),source.registry,1,source.slot),"encode the actual Centurion spawn request");
    dawn::middleware::encoding::bits::Reader reader{std::span<const std::byte>(storage)};std::uint64_t mode{};
    check(reader.skip(writer.bit_count()-36) && reader.read(3,mode),"inspect Centurion ownership on the wire");
    const auto generation=run.c->frame().native[m::asset_index(source)].generation;
    // Reproduce the observed A0D510 order: ordinary actor 37F4201D first,
    // then member-bound actor 24F4201F. The wrong wire mode asks for BOTH.
    // This models the captured native admissions, not a live engine playthrough.
    if(mode==3) {
        check(run.c->admitted({run.run,0x37F4201D,0x0FF90097,generation,20,m::kUnderwatch}),"reproduce the accidental ordinary Centurion");
    }
    const m::EnemyReceipt named{run.run,0x24F4201F,0x0FF90097,generation,20,m::kUnderwatch};
    check(run.c->admitted(named) && !run.c->frame().fault,"the scene-bound Centurion is the only requested actor, not overflow");
    check(mode==2,"named scene cast must not request an additional ordinary actor");
    check(run.c->died(named),"one confirmed Centurion death satisfies its source");
    check(run.c->advance(run.run,++run.now,true) && run.c->frame().enabled
        && run.c->frame().presentation.active && run.c->frame().presentation.event==m::kObjectives[0],
        "opening objective survives Centurion admission and death");
}
static void civilian_door_run_regression() {
    HomecomingRun run(205);
    for(unsigned tick=0;tick<8 && run.c->frame().cinematic.phase!=m::cinematics::Phase::gameplay;++tick) {
        run.now+=250;run.movie();check(run.c->advance(run.run,run.now,true),"civilian regression reaches the opening");
    }
    check(run.c->frame().cinematic.phase==m::cinematics::Phase::gameplay,"civilian regression lands in Underwatch");
    run.visit(m::trigger_area(m::kUnderwatch,"pt_civ_run_b"));
    for(unsigned tick=0;tick<10;++tick) check(run.c->advance(run.run,++run.now,true),"evacuation trigger can be sampled repeatedly");
    const auto& f=run.c->frame();
    for(const auto slot:{57,58,17}) check(!f.native[m::asset_index(m::asset(m::kUnderwatch,43,static_cast<std::uint16_t>(slot)))].active,
        "evacuation trigger cannot start Cayde or his two premature door-running scenes");
    for(const auto slot:{56,59,60,61}) check(f.native[m::asset_index(m::asset(m::kUnderwatch,43,static_cast<std::uint16_t>(slot)))].active,
        "other civilian evacuation scenes remain active");
    const auto& cayde=m::kScenes[m::scene_index(m::asset(m::kUnderwatch,43,17))];
    for(const auto slot:{45,46}) {
        bool found{};
        for(const auto member:cayde.cast) found|=member.registry==m::kUnderwatch && member.type==1 && member.slot==slot;
        check(found,"both front-A civilians remain available to Cayde's native cast");
        check(m::kSpawns[m::spawn_index(m::asset(m::kUnderwatch,1,static_cast<std::uint16_t>(slot)))].sceneOwned,
            "suppressing standalone runners must not spawn loose replacement civilians");
    }
    check(f.enabled && !f.fault && f.presentation.event==m::kObjectives[0],"civilian suppression preserves the opening objective");
}
}
