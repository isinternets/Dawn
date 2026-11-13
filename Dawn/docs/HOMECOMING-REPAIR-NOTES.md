# Homecoming repair verification

## Implemented

- Scene entry waits for an authenticated native selector, not a staging timer.
  Cayde/Ikora/Ward transitions use the bound child performance; Zavala's
  arrival uses its authored handoff action. Ikora's blast observes entry cue
  `2D5973BE`, and Zavala's Ghost waits for the native death-scene completion.
- Scene inputs increment their publication revision. Stop preserves the current
  generation and sends the native stop flag. Revival sends only Zavala's branch.
- Scene combat sources retain authored placement, category counts and tactical
  objectives. Native scenes/named members remain the sole creation owner.
  Observed flag-removal actions never change the population request mode;
  changing that mode can create an additional actor, not release the old one.
- Shaxx's retail auto-rifle rack requires an accepted, current object hold and
  confirmed inventory delivery. The existing transactional inventory path grants
  The Last Dance and Origin Story, reuses already-owned copies, and waits for
  equipment confirmation before progressing. It does not clear the player's gear.
- Weighted template alternatives no longer count as additional mandatory kills.
  Each assault clears independently; wave three cannot begin before wave two is
  cleared. Assault and turbine counts feed the directive progress field.
- Device revision-only replies cannot acknowledge an unmeasured door position.
  Retired objects can be prepared again on a fresh lease. Console scans require
  positive active progress followed by matching completion. Turbine reports use
  destruction order rather than A/B identity.
- Dialogue waits through hero performances and observed native speech windows.
  Required publications remain offered during temporary ownership loss. Assaults
  use combat receipts and two distinct Ward generations, not the old delayed
  evacuation-line gate. No escape jobs or
  queued dialogue can start after cinematic departure.
- Player positions cannot pre-trigger other bubbles or trace a route through a
  cinematic transit. Preparing an unrelated roster does not reset this mission.

## Verification and limits

`Dawn/unit/homecoming_progression_tests.inl` drives all nine sections through
synthetic native receipts and all three cinematics. It deliberately delays scene
and enemy-clear receipts, tests stale/recycled owners and duplicate deaths/uses,
requires both equipment acknowledgements, and destroys turbines C/A/B.

`tools/vanilla/validate_homecoming_scene_bindings.py` checks all four performance
root/action identities and the Ikora signal layout against installed package
payloads. This is read-only. The package speech audit found Cayde row 10 and
Shaxx rows 21–24; these remain scene-owned, not mission-dispatched duplicates.

These checks do **not** establish visual/audio correctness in the running game.
The provided gameplay clip was inspected at sampled frames; the linked YouTube
reference was not available for a full synchronized comparison.

## Required live acceptance pass

Start a fresh Homecoming attempt with the repaired DLL (no saved in-progress
mission state). Confirm the following before calling this end-to-end complete:

1. Kill the breach and post-armory Cabal, including scene cast. Check that an
   early Cabal death does not leave its scene or the following route gate stuck.
2. Hear Cayde/Shaxx without radio overlap; pass Cayde's doorway; hold the rifle
   pickup and verify both weapons arrive and the auto rifle is equipped. Confirm
   Shaxx's veteran Young Wolf greeting and the native dialogue-selection log.
3. Traverse both hangar gates. Check wave counts 0→1→2→3, both Ward performances,
   Zavala's death/Ghost interaction, and actor continuity after revival.
4. Confirm Ikora's entry projectile/blast and Cabal deaths, the bazaar collision
   release, a single Ghost/Zavala pickup exchange, Amanda's arrival line and pickup cinematic.
5. Scan the ship console, destroy turbines in different orders, and finish the
   escape/outro. Check there are no delayed explosions/dialogue after departure.
6. Repeat with slow play, backtracking, and a fresh restart. Check the log for
   `scene_playback`, `weapon_grant`, `vanilla_stall`, and `vanilla_executor` entries.

Native animation completion, rendered HUD counters, real equipment delivery,
damageability, and collision traversal still need this in-game pass. No timer
fallback or synthetic success is used to conceal a missing receipt.

## Build installed for testing — 2026-09-20

- Release DLL build succeeded. Homecoming tests passed in Debug and Release
  (12,832 assertions per run); installed-package validation passed 12 checks.
- Deployed the DLL and matching symbols to both existing DLL locations in
  `C:\Destiny 2 Development`. Installed DLL SHA-256:
  `093FBE3B3FFE0DFB71747CC80221D4F4DEAF095E38962937570C101F145644D9`.
- Previous DLLs/symbols were copied, preserving their relative paths, to
  `C:\Destiny 2 Development\.dawn\backups\homecoming-repair-20260920-050436`.
- Saves, settings and mission scripts were not overwritten. The game was not
  running during installation and was not launched afterward. Existing opening
  video commits and unrelated working-tree changes were preserved.

## Follow-up: doorway, rack, arrival audio and spawn checkpoints

The next live test established that Cayde's scene plays. It also exposed a wrong
Shaxx completion binding: selector `80BEB7EF` child 26 / performer `80B3A3F7`
uses exit-door parameter 1 and does not finish in the recorded armory visit.
The log remained at `Shaxx opens the armory`, so rack 103 was never requested;
inventory delivery was not reached. This was a progression gate bug, not evidence
that an accepted weapon hold had failed.

- Cayde entry now retires blocker `9D8076E4/4/81` in the same publication as
  entry input `6F51AC66`. Only the score/handoff waits for his performance end.
- The separate gun door `23/117` and rifle rack `4/103` activate on armory entry.
  Shaxx's persistent door child is removed from performance gates, including the
  radio-dialogue hold. Observed native speech rows still suppress radio overlap.
- Opening Ghost row 1 has no seven-second request delay: it is offered in the
  first gameplay update after landing, not during loading or the opening movie.
- Read-only inspection while the player stood in the unwanted civilian found
  source 39 (`sq_civilian_wall_sit_c`) at `(-16.410,17.579,-4.056)`, 0.254 metres
  from the player. Shaxx/source 15 was 1.513 metres away. Source 39 is removed
  from the landing cohort; other civilians and Shaxx are retained.
- Twelve run-local spawn checkpoints batch the hallway, armory exit cast,
  hangar, plaza opening, each of its three assaults, boulevard/bazaar, ship
  interior, ship deck/boss, generator interior, and escape. The entire hallway
  through Shaxx is requested on landing. Hero casts are reserved early, but
  scene activation, entry inputs, doors and speech keep their own route gates.
  Each assault remains a separate checkpoint behind its preceding combat gate.
- Repeating a checkpoint does not change source generations or resurrect dead
  actors. The existing full-identity streaming/re-entry checks remain in force.
  Checkpoints reset with the mission run; this is encounter spawn batching,
  not a new persisted save or wipe-recovery system. Logs identify each batch
  with `stage=spawn_checkpoint` and its run/generation/checkpoint number.

The regression traversal deliberately leaves Shaxx's door performance unfinished
for the entire mission. It still requires two accepted equipment deliveries,
post-armory radio after his measured speech window, Cayde collision release
before completion, every spawn checkpoint, three separate assaults and the outro.
Arrival tests check all nine combat sources through Cayde before player movement,
absence of civilian 39, deferred hero scene activation, repeat-safe batching and
terminal early enemy deaths. Actual native spawning, collision, audio and weapon
interaction for this follow-up build still require an in-game pass.

Follow-up verification: Debug and Release each passed 51,078 assertions. The
read-only installed-package validator passed 11 checks, including the explicit
Shaxx exit-door child identity. The full route uses simulated native receipts;
neither test count is a claim of a completed in-game playthrough.

Installed follow-up on 2026-09-20 at 05:30 local time after the Release DLL build
succeeded. The user authorized closing and relaunching Destiny. Process 58500
closed normally. Both existing DLL locations and their matching symbols were
backed up to
`C:\Destiny 2 Development\.dawn\backups\homecoming-checkpoints-20260920-052921`,
along with the pre-fix log. Both replacement DLLs match SHA-256
`A4F702D80F6693E81CBCCCE335F1DA10BBFB01B70F587E68B92AB315CBB5A92B`.
The matching PDB hash is
`3D7DB8E49459B9EBFF21FBF3E16555DA65530315E1EFD98AB393DD4A525E7679`.
Hashes of 31 existing settings/save/script files were unchanged by installation.
The game was relaunched as process 60840. Start a fresh Homecoming attempt for
the live acceptance pass; the previous in-progress mission was not modified.

## Follow-up: plaza handoff, revival, counters and staggered opening

The next run (process 60840) supplied two distinct failure records. First, the
controller waited for Zavala arrival marker `397A672B` at graph `80C3DEBD`
root+`1290`. That action remained dormant even though arrival animation action
59 (`80B8263B`, parameter 0) and its downstream flag-removal action 10 had
finished. The authored completion output is input 63; the old marker waits on
a different branch, input 55. The saved observer now verifies action 10 at
root+`1CC0`, definition `BEB0`, kind `80806297`, parameter 0, flag `1526DC06`.
Selector identity, request generation and weak-handle validation remain required.

At the user's explicit request, a one-time live recovery changed the old marker's
state byte from 0 to 2 after verifying the exact stalled selector and completed
real handoff. The log then requested wave one. This manual recovery is NOT a
natural scene receipt and is NOT part of the installed implementation.

The run subsequently cleared wave one, completed the Ward, requested wave two,
and accepted the intended Zavala revive at `28A6B21F/4/24`. The replacement
Zavala actor was then rejected as overflow in source 6's enemy ledger, which
faulted and disabled the mission. This was not an invalid player interaction.

- Hero sources are excluded from enemy admission/death accounting. Native hero
  creation is unchanged; a revived Zavala no longer overflows an old kill ledger.
- Counted objectives publish both current progress and a target of 3, for tower
  assaults and ship generators. The previous missing target produced `0 of 0`.
- Seventeen encounter checkpoints replace the previous twelve: the opening now
  requests reinforcements at its first-contact, centurion and drop-pod volumes,
  rather than requesting all hallway Cabal on landing. Later encounters retain
  batching. The hangar destruction source waits for its pod to exist, and plaza
  departure requires the measured second hangar gate pose.
- Shaxx's cast is requested when Cayde's performance finishes. Shaxx's scene and
  dialogue still wait for the player's approach.
- Explicit scene-Cabal sources now use ordinary authored population requests
  instead of persistent scene reservations, including the hangar fake-fight
  Cabal without tactical objectives. Ordered scene cast and native placement are
  retained; heroes, frames and civilians are excluded. This is a candidate fix
  for immunity: it still requires a live damage/early-kill test. No global damage
  gate, health flag or friendly immunity is overridden. Captured health flags
  alone did not distinguish the immune Cabal from ordinary killable Cabal.
- The same live log confirmed both armory rewards were acquired and equipped
  after the accepted rifle-rack hold. This validates the earlier pickup repair
  for that run, not every possible inventory/restart state.

Debug and Release Homecoming tests each passed 71,656 assertions. The traversal
includes all seventeen spawn checkpoints, Shaxx's pre-scene presence, three
assaults, hero re-admission without overflow, and the outro. Wire tests inspect
both counted-objective fields and the source mode for every scene-owned source.
Installed-package validation passed 14 read-only checks. These are simulated
controller/wire and package tests, not a completed live acceptance pass.

A missing closing expression on the shared Homecoming roster preparation call
was restored to allow the DLL build, preserving the adjacent Adieu additions.

The Release DLL build then succeeded. Installed at 06:08 local time on
2026-09-20 into both existing DLL locations with matching PDBs. DLL SHA-256:
`EC117564909B1D05109004FD103B78B067ACE11FD3905B4D395FFD603B9DBD4A`.
PDB SHA-256:
`FB11A2B8C5EB218162318851ADAFA09D7082BE64BB8525A80B45B5B7F1B1C4F9`.
The previous files and live mission log are preserved under
`C:\Destiny 2 Development\.dawn\backups\homecoming-plaza-20260920-060339`.
The staged payload and installation receipt are under
`build/coo/homecoming-plaza-20260920-060800`. All 35 protected save/settings/script
file hashes remained unchanged. The user closed the game; installation did not
close or relaunch it. A fresh Homecoming run is still required for live acceptance,
especially scene-Cabal damageability and continuity after early kills.

## Opening regression: duplicate Centurion and lost objective

The 06:08 candidate's broad scene-Cabal population-mode change is WITHDRAWN.
Process 61312 loaded that DLL and reached the first Centurion at log time
176000. Source `9D8076E4/1/20`, owner `0FF90097`, generation 1 admitted ordinary
actor `37F4201D`. Fifteen milliseconds later named member `2/21` created actor
`24F4201F` from the same source. That second actor exceeded the one-actor ledger;
at 176062 the controller reported `fault=1 enabled=0`. This stopped mission
authority and removed the objective. It was a regression in our spawn requests,
not a player interaction or missing route trigger.

- Every scene-owned source now retains reserved mode 1 (wire value 2), before
  entry AND after a native flag-removal receipt. Scenes/named members own actor
  creation; ordinary replacement mode must not compete with them.
- Removed the Cabal-specific ordinary-spawn policy. No population limit was
  raised, no duplicate admission was silently accepted, and no enemies or scene
  completion requirements were skipped.
- The previous known-working log contains one accepted Centurion death and a
  first-contact Cabal death before its flag-removal receipt, while those sources
  were still reserved. Population reservation is therefore not itself evidence
  of immunity. The hangar fake-fight immunity remains a separate live-test item.
- The wave-start binding, hero-ledger exclusion, objective target counts,
  staggered opening batches, Shaxx timing, doorway and rack fixes remain intact.

`centurion_spawn_regression` models the captured native admission order from the
actual outgoing source mode. It failed against the 06:08 implementation and
passes after this correction, with the opening objective still active after the
single Centurion admission/death. Additional wire checks cover every scene-owned
source in all combinations of active, flag-released and killed state; cleared
sources request zero replacements. These model/wire checks do not replace a
fresh in-game opening test.

The captured failing run is preserved at
`build/coo/homecoming-opening-20260920-061642/opening-regression.log`.

Verification: Debug and Release each passed 74,005 assertions; package validation
passed 14 checks; the Release DLL build succeeded. Installed at 06:23 local time
on 2026-09-20, with the game already closed. Both DLL locations match SHA-256
`BB8CAA49FFD605D7C23729DDAFFAB0D940EFB35CECE29F5F266E43DCF73CC0CD`;
matching PDB SHA-256 is
`630B0F4274092D4E46031DD872CA99ECBEEBAE101CB505DE37FD509A2C2D51C4`.
Backup: `C:\Destiny 2 Development\.dawn\backups\homecoming-opening-20260920-061957`.
The evidence directory above contains the staged payload and installation receipt.
All 36 protected save/settings/script file hashes were unchanged. The game was
not relaunched. Fresh live opening verification remains pending.

## Post-revive assault stall: distinct Zavala animation branch

Process 53328, started at 10:14 local on 2026-09-20, passed the repaired opening,
accepted the armory pickup, and naturally completed the initial Zavala arrival.
Wave one cleared; wave two was requested at log time 367922. The intended revive
was accepted at 391203 and restarted scene `28A6B21F/43/2`, generation 2.
All twelve wave-two enemies were recorded dead, but `revival complete` waited
on initial-arrival action 10, which does not run on the revival branch. This
left the visible assault count at 1/3 without faulting the controller.

The live scene root `1AD8C8FA170` had weak identity `1DF9EDB6/418391099`:
arrival action 10 at +`1CC0` was dormant (0), revival animation action 52 at
+`6670` was complete (2), and successor idle action 53 at +`68D0` was active (1).
Installed-package evidence binds action 52 to Zavala animation `80F1FCB6`,
definition `D720`, with no cancellation input. Its output 56 starts action 53,
definition `D7D8`, animation `80BFA697`. Revival sends `B8C5C0A5` without the
initial arrival's additional branch input; generation alone is not a branch ID.

At the user's explicit request, the attempt-specific recovery helper verified
those exact identities/states, then changed only the incorrectly watched arrival
action's state byte from 0 to 2. The selector advanced/retired before restoration,
so the old pointer was not written again. No enemies, health, kill receipts or
assault counters were changed. The resulting log-time 545000 completion is an
ARTIFICIAL recovery receipt, not evidence of natural arrival-branch execution.
Wave three was then requested at 548250; all assaults cleared and Boulevard
checkpoint 12 began at 631047. The live run subsequently entered section 6 at
820500. No door change was made for the subsequently withdrawn door report.

The permanent observer now selects arrival versus revival from the controller's
accepted revive state. Revival requires the completed action-52 animation and
the active action-53 idle, with verified node/resource/input identities. Exact
scene generation, selector weak identity and post-read identity checks remain.
The controller rejects receipts from the wrong branch even for the same scene
generation. There is no timer, memory-write or forced-completion fallback in
production code.

The command-ship sighting line (row 34) now requires both the hangar-window route
trigger and confirmed native presence of the command ship. In this live log the
window was crossed at 230078, before the ship was requested at 262469 and reported
present at 262703. This prevents premature speech; exact visual/audio alignment
of the new build still needs a fresh in-game check.

Debug and Release Homecoming tests each passed 74,026 assertions. Tests cover
branch selection independently of generation, rejection of wrong-branch
receipts, all nine completion/idle state pairs, and both ship-sighting gates.
The full simulated route still completes three assaults and all seventeen spawn
checkpoints. Read-only installed-package validation passed 21 checks. These
tests do not constitute a natural live playthrough of the new observer.

## Ship generator, objective variant, and ending (same 10:14 attempt)

The generator entrance requested slot 59 at t=1026203. Its actual native
controller reached position 1.0 with accepted revision 2; the host retained
that same measured float and revision but had cleared `poseKnown`. Type-23
Sense transmits float and revision as independent sparse fields. The initial
1.0/revision-1 report was followed by revision 2 alone; invalidating the float
on that delta stranded `generator entrance open` indefinitely. Consequently
objects 142/144/146, the generator animation devices, objective 11 and speech
86 were never requested until recovery.

The permanent decoder now retains each measured field across omitted-field
deltas. It rejects an older position revision before mutating any channel,
never substitutes a requested position for a measurement, and never treats an
initial revision-only report as a known pose. Mission selection clears caches.
Ordinary door acknowledgements still require the exact commanded revision and
measured target. First activation no longer implicitly sets `snap`: DF6C70's
snap branch bypasses its normal interpolation/animation path. This applies
to the console doors and generator machinery, but the reported orange-barrier
visual/collision issue still needs fresh in-game verification; it is not proven
resolved merely because the device reports position 1.0.

Attempt-specific live repairs (user authorized, no production memory patches):

- Door 59's scope, native weak identity, physical root, owner, actual 1.0/rev2,
  sensor Sense and installed DLL hash were checked. Only the host's lost
  `poseKnown`/`acknowledged` flags were restored. The generator sequence then
  started at t=1781359 and native speech dispatch 86 occurred at t=1781437.
- The player subsequently destroyed all three turbines. Actual controllers and
  Sense reported 0.4/rev4, while setup Auth remained 0.1/rev2. The old equality
  check rejected all three. After validating the three physical identities and
  terminal states, their three host completion latches were reconciled. No
  health, damage, turbine positions, or objective records were fabricated.
  Shutdown began at t=1880812; escape objective/checkpoint followed at 1880922.
  Native speech dispatches 88/90/91/92 were observed. Dispatch is NOT proof of
  audible playback; the user's silent-Ghost report remains a live-test item.
- Escape door 60 had the identical sparse-confirmation stall. Its verified
  actual 1.0/rev2 receipt was restored without touching a route trigger. Section
  8 began at t=2120562. The already-visited finale route then requested the
  ending normally. Native outro registry 42E8F541 acknowledged start (5239) at
  2122203 and completion (1685) at 2237125. The process later exited; this agent
  did not close or relaunch it.

Production turbine receipts accept terminal 0.4 at the setup revision or a
newer native revision, only on managed, active, synchronized devices. Stale
pre-activation reports, partial positions and duplicates cannot add progress.
There is no timer, forced kill, or automatic completion fallback.

The missing HUD counter was a separate presentation error. Installed directive
table 80B508FE, event F0D48F30, has two **36-byte** values: variant 0 contains the
description only; variant 1 contains `Exhaust turbines destroyed` (string key
44FBFF68) and flags=1. The host always selected variant 0 despite supplying 0/3.
Homecoming now publishes discriminator 1 for that event and keeps discriminator
0 for the assault counter. The extraction helper's incorrect 32-byte value
stride is corrected as well. Native 1008B40 selects the value at 36*discriminator.

Stationary interior Cabal had local movement authority but no tactical task
binding. The final-build catalog assigns combat sources 36-52 and 54-55 to
authored task `2D322467/3/1` (`obj_deck`). Its 24 rows include interior providers
261-278, not only the exterior deck; native reachability costs choose the row.
Source 53 (Ghaul's cinematic cast) is excluded. Spawn counts, generations,
scene reservation modes and death ledgers are unchanged. No live combat-task
assignment was performed, as requested.

Verification for this candidate: Debug and Release each pass 113,025 assertions,
including unchanged-pose door bootstrap, value-before-revision, reset/foreign
owner rejection, stale-packet atomic rejection, newer turbine death revisions,
duplicate deaths, both HUD variants, native task-cost selection, and the full
nine-section/outro simulation. Read-only package validation passes 31 checks,
including the counted objective variant and ship-interior task providers.
These checks do not prove audible dialogue, rendered HUD, barrier collision,
or combat movement in the new build. A fresh natural playthrough remains needed.

## Transcript pacing, duplicate pickup exchange, and veteran dialogue

The user's supplied retail transcript supersedes the requested video comparison
for this pass. Its gameplay events are the ordering reference, not elapsed
walkthrough timestamps. This is not a frame-accurate audio comparison.

The duplicate Ghost/Zavala/Amanda pickup exchange is row 72, selector 06F8A841.
Ikora actor performer 80B3A848 already schedules it at offset 29E4, following
rows 66-69. The host also queued it after the performance. Row 72 is now
native-owned in the generated catalog and the host only updates the objective
at that handoff. Regression tests prohibit another host request for that row.

Other beat changes:

- The Cabal identification follows the wall breach. Their purpose explanation
  follows clearance of the first-contact cohort, not a fixed four-second delay.
- The Red Legion conversation belongs to the corridor exit toward the plaza,
  not the hangar-reveal music volume. Removed the forced hurry lines 38/39.
- Plaza order is first assault, first Ward warning/barrage, second assault,
  second Ward warning/barrage, then the final assault. The first shield warning
  (53) was missing. The second Ward uses a distinct scene generation and rejects
  a completion receipt from the first. Existing death/revive handling follows
  the second barrage; this is still mandatory, unlike the transcript's optional
  revive, and is not claimed as exact retail parity.
- Removed the 20-second delayed evacuation line as a gate before wave one,
  the extra three-second last-wave pause, and five-second report delays. The
  three-second native cohort-clearance settle window remains. Row 55 must
  actually be submitted and finish its authored window before row 57.
- Leaving the plaza no longer forces delay-leaving nudges 60/61. The ship's
  Cayde status exchange (82) follows the damaged-hall stair trigger instead of
  a fixed 20-second delay from the console scan.

Veteran dialogue uses three real conditions in bank 80C2AF61. Expression pool
81319324 resolves 63533000 to dense flag 2024 (City/Tower), D12EC6F2 to 2027
(Amanda's tools), and EAB8395B to 2028 (Shaxx's Young Wolf greeting). Flag table
81319321 verifies their keys 2D4AA2EA, 0D991A39, and 0244DDE2 respectively.

Only Homecoming gameplay activity 266 leases these evaluated flags as true.
Opening video 265, Adieu 288 and other missions do not acquire them. The existing
campaign/strike flag-4 policy is unchanged. The existing native family-five
commit, cache copy and derived-state rebuild apply the overrides. Native
evaluators must confirm the requested flags before a direct launch proceeds.
The previous values (including absent overrides) are restored on departure;
unrelated overrides and durable Destiny 1 accomplishments are not changed.
The four-flag lease is atomic if native storage is full or malformed.

Debug and Release Homecoming tests each pass 117,565 assertions, including the
full route, both Ward generations, counter timing, route speech anchors and
duplicate-dialogue prevention. Debug and Release campaign/strike tests each
pass 561 checks, including veteran selection, previous values 0/1/2 or absent,
repeat selection, restoration and insufficient-capacity atomic rejection.
Read-only native-package validation passes 45 checks, including the embedded
pickup exchange and all three veteran condition mappings.

The broader Debug mission-launch lifecycle executable passed the Homecoming
opening/return path, then failed its hook-wait assertion on the separately
modified Adieu opening: the launch reported `manualRejected` instead of
`preparing`. It used the installed 81327CF0 activity-table fixture. No Adieu
source was changed in this pass, and this broader suite is not recorded as a
pass. The temporary diagnostic that identified the failing mission was removed.

These checks establish source behavior and native asset identities, not live
audible playback. Veteran branch selection, speech timing, both Ward replays,
HUD rendering, the orange barrier and ship combat movement still need a fresh
natural playthrough. The candidate is staged only; this pass does not install,
patch live memory, close the game or relaunch it.

## Post-armory protected Cabal and early Shaxx acquisition

The subsequent live report identified two nearby Legionaries, Underwatch
sources 112 and 114, cast parameters 0 and 1 of scene 111
(`sc_frame_cover_shoot_die`, selector 80B82769, graph 80C3DF11). Both had local
movement authority. Read-only inspection of PID 63384 found graph root 5528,
opening animation action 7 and wait action 15 active, and both native
1526DC06-removal actions (11 and 18) still dormant. No live writes were made.

The mission activated this scene but never sent any of its external inputs.
Native event 38857CF3 supplies inputs 31 and 33. Input 31 exits animation 7,
which runs one-shot animation 8 and then starts flag removal 11 for parameter
0. Input 33 exits wait 15 and starts removal 18 for parameter 1. The mission
now sends that event once after the current selector is observed at pt_postgun.
The section handoff waits for this request so it cannot abandon the release
step. The native graph still owns animation completion and flag removal.

The later 18EF2ABC/B687F07B events are not used as a generic unlock: the former
advances into another scripted death/staging branch which re-adds 1526DC06 to
parameter 0. No health offsets, global damage hooks, population ownership modes,
enemy counts, or spawn generations were changed. This repairs the observed
missing handoff; actual damage/death behavior still needs a fresh native run.

Shaxx was only reserved after Cayde finished. Reserved sources do not create
scene-owned actors themselves; scene 14's acquisition action creates him, and
that scene previously waited until the player's approach. Cayde's entry now
requests both the Shaxx checkpoint and scene 14. The native graph's init input
30 reaches acquisition input 4 without the entry event. Entry 6F51AC66 remains
gated on Cayde's completion, the player's Shaxx approach and the live selector.
Its wait action 14 drives the greeting; early acquisition does not send it.

Debug and Release Homecoming tests each pass 123,286 checks. The route simulation
now requires active Shaxx acquisition during Cayde's unfinished performance,
with no Shaxx entry/speech, and exactly one post-gun release event. It supplies
separate native flag-removal receipts, rejects stale/duplicate receipts, maps
only parameters 0/1, and admits/kills each Cabal once. Existing wire tests keep
scene sources reserved through release and death, preventing duplicate actors.
Read-only installed-package validation passes 70 checks, including both release
branches and Shaxx's independent init/entry paths. These are not a live damage,
rendered spawn or audible dialogue verification. This repair is staged only.

## Follow-up: installed post-gun handoff did not resolve immunity

The user retested DLL E855F3BD841717C397AC06058DECAEA4B92E9B423E973978816F4FA232B3B685
in PID 44936 (started 2026-09-20 11:49:50) and reported the same immunity.
Read-only log inspection confirms scene 111 started at t=216453, reported
`released=0002` at 216562 and `released=0003` at 217984. Thus the first event
was delivered and both watched 1526DC06 removals executed. That receipt is NOT
proof that an enemy can take damage. The preceding diagnosis and test wording
incorrectly identified source 112 as a Cabal.

Authenticated binding names establish the actual cast order:

- Parameter 0, source 112: `sq_frame_post_gun` (friendly frame).
- Parameter 1, source 114: `sq_red_guard_post_gun` (Cabal).
- Parameter 2, source 115: `sq_red_guard_post_gun_jump` (another Cabal source).

Only 112 and 114 were present in the initial read-only actor census. Both were
locally owned; source 115 was not observed. Do not claim that the two visible
participants are two Cabal, or that this census identifies the user's other
reported immune enemy. A location clarification was requested asynchronously.

Graph 80C3DF11 sets TWO flags on parameter 1: 1526DC06 and F9521E4E. The installed
38857CF3 event removes only the former. Parameter 1 then waits in action 19,
definition 6398, for input 34. Event 18EF2ABC emits inputs 32 and 34. The Cabal
branch proceeds through action 20 (6428), emits inputs 23/24, updates its target
in action 24 (6660), and removes F9521E4E in action 29 (6938, actor parameter 1).
No host request currently sends this event. The simultaneous re-addition of
1526DC06 is to parameter 0, the FRIENDLY FRAME, not to a second Cabal. Avoiding
this event based on that cast misidentification left the native sequence
incomplete. The first-contact scene independently removes both flag hashes.

The next repair must complete the correct authored second-stage transition
and distinguish actor-control release from full enemy combat/damage readiness.
The existing simulated deaths and generic 1526DC06 observer do not establish
live damage acceptance. An actual enemy hit/death check is required before
calling this fixed. The exact low-level meaning of either hash has not been
proven by tracing a native damage rejection; do not label either a verified
health-bit enum. Source ownership must stay reserved to prevent duplicates.

The user continued to the military/plaza while this inspection ran. Scene
111's old selector reference was recycled, so no further reads through that
stale selector or writes to it were made. This follow-up made no production
code, installed-file, process-lifecycle or live-memory changes; it records the
failed live acceptance and corrected diagnosis.

## Same-run console barrier: live reset failed acceptance

The user requested a live-memory repair of the orange barrier after the Ghost
scan, in PID 44936 using the same installed E855F3BD candidate. The scan was
accepted at t=614203 (`generation=2`, progress1, scanned1). Devices 56,57,167
were published and physically acknowledged at position1/revision2. This is
not the previous missing-pose receipt bug: all three host states have
poseKnown and acknowledged set. Entity authority is already local.

Standing against the barrier provided a spatial identification: player
(30.473457,-271.630035,71.607666) was 1.49 units from entity16FAA04A, placement
80C3B4B7/25, GUID D8FFC2F1F8979C65, at
(29.458183,-271.315216,72.656830). This is ship device56, not an inferred
neighbor or the distant second pod door.

Attempt-specific identities (must NEVER be reused without fresh validation):

- Sensor 2B65489A200, descriptor80B511A4/80804F46/+278,
  scope2D322467/23/56.
- Generic device 2B653825D00, descriptor80C22C67/80803910/+A78,
  salted reference81D888EB/75F9E32B, owner16FAA04A.
- Behavior 2B6538266F0, descriptor80F2769A/808084E9/+D20.
- Data graph 2B653827530, descriptor80B9F35D/80809790/+798.
- Installed module7FFD84AA0000; native state asset332 at module+18CCB18+332*48.
  These RVAs were resolved against this installed PDB, not the prior build.

With user authorization, only device56's host position, command generation,
acknowledgement bit and publication revision were changed. It moved from
position1/revision2 to position0/revision3, and then back to position1/revision4.
The native measured position and host acknowledgement reached each target.
The user confirmed the barrier was still blocked at BOTH endpoints. Position1
is restored; door57, console167, enemy state, objectives, scan completion and
mission progression were not modified. No direct collision/render writes or
remote native calls were attempted. Helpers are attempt/hash/identity gated.

This disproves a simple missed position update or reversed endpoint as a
sufficient explanation. The barrier's behavior/collision handoff still needs
investigation; the live intervention did NOT unblock the player. Do not report
an acknowledged device pose as successful physical removal. No permanent
barrier source fix or new installation was performed by this intervention.

The second immune-Cabal location was clarified as farther down the hallway.
Military cover scenes70/72/74 bind friendly frames69/71/73 as parameter0 and
Cabal66/67/68 as parameter1; graph80C3DD7F has no F9521E4E-setting action for
parameter1. Its 18EF2ABC event releases input46's parameter1 wait, then actions
36/38 hand off targeting. Throw scene77 binds frame75 and Cabal76;
graph80C3DD81 explicitly retains parameter1's F9521E4E until event18EF2ABC
releases input24, then action20 removes it. None of these military events are
currently sent. This is additional missing native staging, but it does not
identify which source was the user's second immune Cabal or prove that all
these graphs share the same damage-rejection cause.

## Staged follow-up: both ship barriers, both Cabal locations, early reactor visuals

2026-09-20: source changes and a Release candidate are staged only. The user's
no-restart/no-install request remains in force. This follow-up performed no
live writes, injected calls, game shutdown, restart, or installed-file replacement.

### Cabal: complete each authored handoff

Scene111 now waits for its observed parameter1 opening release AND the active
native wait19 (80C3DF11/root5528/node24C0/definition6398/input34) before sending
18EF2ABC. A separate receipt records completed F9521E4E removal on parameter1;
the armory handoff waits for that receipt. The earlier 1526DC06 removal is no
longer treated as equivalent. Parameter0 is the friendly frame; source114 is
the reported post-armory Cabal. No release is fabricated for unobserved source115.

The farther-hallway cover scenes70/72/74 and throw scene77 each receive
18EF2ABC once their own parameter1 wait is active. They do not wait for one
another. Cover graph80C3DD7F uses wait35/node4AF0/definition9048/input46;
throw graph80C3DD81 uses wait12/node1760/definition4838/input24. The latter
removes F9521E4E from Cabal76. These cover every authored encounter in the
reported farther-hallway area without pretending the screenshot identified
one particular source. Scene ownership, spawn generations and enemy counts
are unchanged; no ordinary replacement actors or global damage hook is added.

### Barriers: retain the authored open state on both exact placements

The captured door56 was physically at position1, with its opening curve
finished and no presentation writer, but its named-state provider remained
in index1 (BD855C4F). Reissuing the same device position was insufficient.
The fix targets only list80C3B4B7 records25/26, GUIDs D8FFC2F1F8979C65 and
1EB7FCA5F866CD3A, corresponding to ship devices56/57.

The existing game-thread poll waits for the accepted Ghost scan, matching
device generation, acknowledged actual open pose, current native revision,
and the native curve's final zero sample with no active writer. It then uses
the native named-state setter A1FBB0 on the REAL 80808870 provider80B9F367
to select authored name697C33EC/valueBD855C4C (index0). Physics80F27699 is
enabled in BD855C4F. After readback confirms index0, native variable setter
576420 retains presentation BBC7B08A at its authored zero endpoint, following
the existing One AU completed-curve retention pattern.

The state setter's verified prefix begins 40 53; the older bazaar-door helper's
prefix/component must not be reused. That separate helper is unchanged here.
Both setters, graph headers, state layout, component weak serials, owning
entity, local authority and exact placement are checked. Unsupported states
2/3 are untouched. Each door can become ready independently. No raw physics
body deletion, model hiding, noclip, host timer, objective skip or scan bypass
is used. Native state readback is not proof of physical passability.

### Reactor: presentation starts in the preceding room

pt_engine_room_lower now requests objects148/149, the generator assembly
devices61/62/63/64, vents65/66/67 and lights159/160/161/162. These requests
do not depend on objective11. Chamber entry pt_destroy_battleship still owns
objective11, the sabotage cue, shootable objects142/144/146 and turbine
devices143/145/147. Updating the objective does not recreate the preloaded
props. Native destruction receipts still own the counter and shutdown.

### Verification and next gameplay acceptance

- Homecoming Debug and Release: 141,274 checks passed in each configuration.
- Read-only scene/package validation: 111 checks passed, including all five
  active-wait bindings and the missing second-stage paths.
- Read-only barrier package/native validation: 23 checks passed.
- Full Release x64 DLL built successfully; targeted diff whitespace check passed.
- Regression route includes deliberately delayed scene77, independent cover
  releases, separate opening/damage receipts, stale/recycled receipt rejection,
  early reactor presentation, chamber-gated targets, unchanged prop generations,
  all three assaults, all turbines and the outro. These are simulations.

Stage: build/coo/homecoming-barriers-cabal-reactor-20260920. Its README records
binary hashes. The full DLL includes the existing working tree, including
preserved unrelated Adieu work; it is not an isolated Homecoming binary patch.
No broader mission-suite success is claimed.

After the user authorizes installation, start a fresh mission and verify:
the post-armory Cabal and farther-hallway Cabal take damage and die without
duplicates; both orange fields disappear and are passable after the scan;
the reactor beam/assembly is already active on approach without premature
objective/counter changes. Native log receipts help diagnose failures but
are not substitutes for these acceptance checks.

Installation update, 2026-09-20 12:42 EDT: the user explicitly requested this
staged build be installed. Destiny2 was closed. Both root and bin/x64 copies
of steam_api64.dll and its matching PDB were backed up and hash-verified,
then replaced and verified against the staged hashes. Installed DLL SHA-256:
B90A5BAAA4D93F0C442F059DE8725F744E490A86D746FEB8A0D54C546EDEE9BD.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-barriers-cabal-reactor-20260920-124234.
The stage's installation.json records all four targets and before/after hashes.
No game shutdown, relaunch, live-memory writes, save/settings or script changes
were made. Installation is complete; native gameplay acceptance is still pending.

## Follow-up: live ship-entrance authority and premature civilian run

2026-09-20, PID60972 (started12:44:16 EDT), installed B90A5BAA candidate.
The user requested a read-only combat/movement check as first priority and a
build fix for the two civilians running through Cayde's closed door.

At player position (57.6462,-492.9885,88.9210), the four nearest sampled
mission actors were ship sources36/37/38/40 at distances11.0/14.7/16.0/21.6.
Each had local ownership, a matching 808082EC AI owner/actor identity, committed
source generation1, and matching requested/applied type3 slot1 task scopes
with authored rows3/7/3/0 and valid native task groups. Sources36/37/40 moved
3.68/3.05/3.71 units over two seconds; source38 did not move in that sample.
Thus a blanket missing entity authority or missing AI/task is not supported
for these nearest actors. This does not establish every navigation decision.

Farther sources included locally owned actors with AI but row=-1 and no task
group. Logs subsequently assigned ship source41 row12 at t768515 and source42
row15 at t788281. Do not claim these unassigned actors lack entity ownership,
or conclude every stationary actor is healthy. No live task/authority writes
were authorized or performed. A later player sample (-1.6203,-730.2797,50.7040)
had no mission actors within35 units, so it cannot test nearby combat movement.

The standalone front-A run scenes57/58 bind civilians45/46; both are also
members of Cayde's scene17. The evacuation trigger previously launched these
runs before Cayde's door beat. The graph now suppresses only those standalone
runs, preserving Cayde's full native cast, other evacuation scenes56/59/60/61
and speech7. No arbitrary movement lock or loose civilian spawn is introduced.

Debug and Release tests each passed141303 checks, including a new repeated
evacuation-trigger regression. Full Release DLL build passed. The candidate
is staged at build/coo/homecoming-civilian-door-run-20260920, with hashes and
scope in its README. Installed files/process state are untouched; no rendered
acceptance is claimed before a fresh-mission retest.

## Follow-up: duplicate Ward effects and bounded entrance discovery

2026-09-20. User supplied Destiny 2_2026.09.20-12.50.mp4 and approved
including a scoped performance optimization. No live-memory writes, game
shutdown/relaunch or installation were performed in this follow-up.

### Ward visual ownership

Reviewed quarter-second frames from the first two casts and the third/death
performance. The first two show an early purple field while Zavala still
holds his rifle, followed by the native casting effect. The third is a
different, already working native performance; its graph and events remain
unchanged. Review frames are under build/coo/homecoming-ward-review-20260920.

Both regular Wards previously enabled plaza object18 (bubble_shield_1) at
the same time as event C021F76C. Object18's descriptor80B50F39 references
shield resource80B826F1. However, scene4 graph80BEB7A1 already launches
performer80B3A2D8 -> runtime80B3A858. Its own timeline starts animation
80B3A2D3 at authored time3.0 and spawns shield variant80B826F4 at time5.3
(runtime nodeA20 / definition16C0). These shield resources share components.
The mission-requested shield was a separate, premature instance, not the
native performer's casting effect.

Removed only the two object18 enable commands. The native performer now
exclusively controls the regular Ward's effect timing. No new host delay,
native offset reader, cancellation rule, scene completion gate, missile
timing or mission progression override was introduced. Existing object18
clear commands remain defensive cleanup. Package validation pins the
performer/animation/shield relationship; route tests assert the duplicate
stays off throughout both plaza sections and that both Ward generations
still finish through natural completion receipts. Actual visual alignment
and protection during incoming fire require a fresh in-game pass.

### FPS investigation and scoped optimization

homecoming_entrance::update was doing the 8192-slot entity scan on every
native device tick, including callbacks for unrelated or already owned
devices. Its ten-second log heartbeat did not throttle scanning. This is a
code-proven repeated workload, not a measured attribution of the user's
90-100 vs approximately200 FPS report.

The Homecoming-only path now checks the callback's exact salted entity
directly, with an immediate repair if it is an eligible unowned door.
Full-table fallback discovery is capped at once per500ms for an unchanged
run/generation/section/table. New worlds/checkpoints/sections invalidate the
schedule immediately. The fallback still discovers streaming doors that
have not received callbacks. Underwatch/Armory skip entrance scanning
because the allowlist has no eligible placements there. Setter signature,
authored placement, ownership, fresh row/table identity and current mission
request checks remain in place before each grant. No door position, power,
lock, mission timing or other mission's entrance policy was changed.

The deterministic stress test makes128000 callbacks over two simulated
seconds and permits only four full scans. Tests cover immediate callback
repair, idempotency, stale/recycled handles, changed requests, changing
tables, inactive periods, section/generation/run changes and fallback
discovery. This is not a frame-time benchmark.

Windows rejected the attempted read-only CPU trace with0xc5585011
(performance-profiling policy unavailable); WPR subsequently reported no
recording. Therefore no CPU sample attribution or recovered FPS is claimed.
The diagnostic work and video decoding also ran concurrently with the game
for part of the investigation; compare FPS after those tools have stopped,
using the same location, camera, graphics settings and encounter state.

### Build verification and handoff

- Homecoming Debug and Release:143070 checks passed in each.
- Read-only scene/package validator:124 checks passed.
- Read-only barrier/package/native validator:23 checks passed.
- Full Release x64 DLL built successfully; targeted whitespace checks passed.
- Stage:build/coo/homecoming-ward-performance-20260920 (README has SHA-256).

This stage includes the uninstalled civilian-door-run change and the earlier
Cabal, dual-barrier and reactor-preload fixes. It preserves the existing
dirty working tree, including unrelated Adieu edits. Installed DLL remained
B90A5BAAA4D93F0C442F059DE8725F744E490A86D746FEB8A0D54C546EDEE9BD.
Build/test success is not rendered acceptance or a cross-mission test claim.

Installation update, 2026-09-20 13:20 EDT: the user explicitly requested
installation of the Ward/performance candidate. Destiny2 was already closed.
Both root and bin/x64 DLL/PDB copies were backed up and verified, replaced,
then verified against the staged SHA-256 hashes. Installed DLL:
D7BCB717630F4AEFAA7A531EB6CEE66635E729EF58A5514DDFF2467E18126737.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-ward-performance-20260920-132054.
The stage's installation.json records all four targets and before/after
hashes. No game shutdown/relaunch or live-memory writes were performed.
In-game Ward/protection and FPS comparison remain pending a fresh mission.

## 2026-09-20: scene starts, opening observer cost and camera-less orbit

The stairwell frame/Cabal performance now starts from Shaxx's actual door-opening
child (80BEB7EF/52A8, node3920, definition6718, child80B3A863/C60), not the
post-gun trigger. The post-gun volume still owns the evacuation announcement.
Early quiet Shaxx staging and the armory pickup do not wait for this persistent
door child to finish. Parent/child salted identity, generation and cancellation
checks remain mandatory. The package validator proves the child is the opening
performance, rather than the closed-door idle.

Plaza props slot5 (specops_plaza_ship_battle.o_cabal_ship) now enters when Ghost
row40, "Look at the Traveler", is accepted by the native dialogue dispatcher.
It no longer starts on plaza section/objective arrival. Queueing the line alone
does not start the ship; its voice-window completion is not required either.
The other skybox props and the Zavala arrival scene are unchanged.

### Opening-area performance change

The Homecoming observer previously walked every selector's action table twice
per native Scene update, with multiple ReadProcessMemory calls per action.
The package-derived scene_actions.h now pins only the relevant speech/removal
actions:30 across38 inspected graphs. Each watched action needs one bounded
runtime header/state read, retaining the parent weak-handle, generation and
root revalidation. Exact graph/root/node/definition identities fail closed.
The watch-table checker compares the complete generated inventory against the
installed packages. No native scene action or actor has been suppressed.

Only19 of60 catalogued scene descriptors need observation: the union of native
speech/flag actions and every explicit scene observation in the mission graph.
The41 decorative descriptors now skip the controller mutex and selector reads.
No polling delay, stale pointer cache, graphical setting or dialogue timing was
introduced. Both combat-hold/release paths remain watched. This reduces a
code-proven repeated workload; it is NOT a measured claim of restored200 FPS.
Earlier OS GPU/CPU samples were not localized to the subsequently reported
opening-only slowdown. WPR tracing was unavailable. Compare the same opening
location, camera, settings and encounter state after installation.

### Exit investigation and launch fix

Read-only capture of PID63928, installed DLL D7BCB717..., found a queued launch
(status1,busy1,activity266) while Dawn's last camera publication remained at
step27 for more than19 minutes. Native logs had already completed the orbit
transition (step29). Launch processing and dialogue-lease cleanup previously
depended exclusively on this absent camera callback.

The existing signature-checked native primary-session update now also polls
launches in exact orbit state29. It rejects secondary sessions, cleanup/loading
and gameplay, and never invokes this path from Present or a worker. A shared
atomic reentry guard prevents camera/session callbacks or native reentry from
submitting the same launch twice. One bounded orbit-owner diagnostic is emitted
per orbit entry. Camera-less orbit cleanup, replay and reentry are unit tested.

The Dawn opening mask was released and Homecoming prologue was complete in the
capture. The native fallback-orbit warning also occurred before Homecoming,
when the user reports that the ship displayed normally; it does not establish
the black-background cause. The launch starvation is fixed in code. Recovery
of the rendered orbit background is still unverified; no speculative native
flags or arbitrary fade channels were reset.

### Validation scope

- Homecoming Debug/Release:150308 checks each, including full simulated route,
  Shaxx door start, dispatch-gated ship, observer inclusion and identity guards.
- Homecoming launch lifecycle Debug/Release:99 checks each (--homecoming-only),
  including no-camera cleanup/replay and reentrant update rejection.
- Installed scene validator:132 checks. Barrier validator:23 checks.
- Generated action inventory: exact match across38 graphs /30 actions.
- Full Release x64 build and targeted whitespace checks passed.
- The unrestricted shared launch suite reaches a separate existing Adieu
  profile failure: profile12 returns manualRejected because its declared
  forced spawn is the absent-spawn sentinel. Its dirty implementation is not
  changed by this Homecoming repair. Do not report that unrestricted suite as
  passing or claim a rendered playthrough from these tests.

The user authorized closing/installing/reopening when ready. Stage and install
receipt:build/coo/homecoming-orbit-scene-performance-20260920. Previous Cabal,
barrier, reactor-preload, civilian-run and Ward fixes are retained. Existing
unrelated working-tree changes remain intact. Rendered scene timing, orbit
background and same-location FPS acceptance require a fresh in-game pass.

Installed at2026-09-20 13:56 EDT after a graceful close of PID63928. All four
root/bin-x64 DLL/PDB targets were backed up and hash-verified before replacement,
then rehashed against the staged artifacts. DLL SHA-256:
D15CFA52CEB276733DA18417658C9A5110BC2EE342978CD4C3FA27F7869C48B0.
PDB SHA-256:38F02D4A1E2910B4916CFAB1B879D2AC25843B64E788F160FFFCD3238EAFC3F9.
Backup:C:/Destiny 2 Development/.dawn/backups/homecoming-orbit-scene-performance-20260920-135559.
Game relaunched with its previous executable and no added arguments.

## Microstutter follow-up (2026-09-20)

The focused PresentMon capture on PID45212 confirms recurring hitches: median
frame 5.4382ms, p95 24.9031ms, maximum 65.4162ms. Of162 frames above16.7ms,
160 align with periodic authority-apply batches within the clock's16ms
resolution; batches recur about125ms apart. This identifies the update path,
not the exact share attributable to each function. The user reports recovery
after Shaxx's armory. Details and raw capture are in
build/homecoming-microstutter-investigation-20260920.md.

The follow-up removes three proven sources of redundant work:

- Completed Homecoming receipt watches stop before resolving native selectors.
  Retirement requires every consumed start, speech, performance, entry cue,
  combat hold and damage/combat release receipt for that scene. The complete
  definition-to-graph mapping is extracted from installed packages. This is
  observer retirement only: native scenes, actors and authority still run.
  A new scene generation resets its receipts, including Ward/revival replay.
- Gate diagnostics track the complete committed tuple per component, definition
  and self handle. Alternating unchanged bound/unbound gates no longer look
  like changes. A nonblocking, bounded128-entry cache retains actual changes
  and a ten-second heartbeat. Native apply and Launchpad observation are never
  suppressed. No settings or logging sinks are disabled.
- Homecoming root lookup now uses exact key/tag resolution and copies only the
  matched descriptor. The old extraction-order hint could fall back to copying
  thousands of large records each periodic publication. Complete descriptor
  validation remains in place and no lookup persists across catalog changes.

Mission publication cadence, delivery transactions, generation/weak-reference
checks, enemy population, dialogue and graphics settings are unchanged. The
regression tests check operation counts and required observations rather than
flaky wall-clock thresholds. This is a performance candidate, not a claim that
200FPS or smooth rendered play has been verified. Initially staged while the
user continued testing; no live-memory edit was made.

Debug and Release Homecoming suites passed151787 checks each. Full Release
build,132 scene bindings,23 barrier bindings and38-graph/30-action inventory
validation passed. On the user's subsequent explicit request after closing
the game, installed and verified all four root/bin-x64 DLL/PDB targets at14:23
EDT. No relaunch. Receipt:build/coo/homecoming-microstutter-20260920/installation.json.
DLL SHA256:E2AA518BF6DF864A186A0C9E0DEE26273C473B94A8D0236C777DDE47A911C1F1.
Backup:C:/Destiny 2 Development/.dawn/backups/homecoming-microstutter-20260920-142325-f7bb43a3.
Post-install same-location performance acceptance remains pending.

## Ship barrier ownership regression and launcher labels (2026-09-20)

Read-only capture of both post-scan ship fields proved that their open device
commands and 3.5-second curves had completed, and entity authority was local,
but the named-state provider remained in blocking state 1. A1FBF0 rejects the
change through 3F3C00: the provider's bundle has a separate network ownership
record whose authority bit is unset. Resending the device command cannot fix it.
Evidence: build/coo/homecoming-barrier-diagnosis-20260920/evidence-69004.json.

The game-thread handoff now resolves that bundle's network record through
3F7A20, validates its self/bundle/entity identity and weak reference, and uses
the native ownership setter 4DAD00 before the ordinary named-state setter.
This also synchronizes entity authority and native replication bookkeeping.
The complete ownership routine, its two callees, bundle lookup and permission
gate are fingerprinted. No raw flag writes or global permission bypass are used.
Only the two exact authored placements, following the acknowledged Ghost scan,
matching device revisions and naturally completed curves, are eligible. Ownership
lasts with the native object; no record pointers survive request/cache resets.
State zero must be read back before publishing the open visual. Rejections now
produce a reason once per failure transition and retry at most twice per second.

The launcher displays Exodus (02) instead of Adieu, and 1AU (16). Red War rows
are displayed in campaign order while route indices, activity hashes, internal
Adieu identifiers and other campaign numbers remain unchanged.

Validation: Debug and Release Homecoming suites each passed 151852 checks;
24 isolated native-code checks reproduced the original denial and reached the
real state-mutation entry after the native ownership grant for both barriers.
Those tests use synthetic resource data and stop before world/physics mutation;
they do not establish in-game passability. Also passed: 28 barrier bindings,
132 scene bindings, exact 38-graph/30-action inventory, Release x64 build, AMD64
and all 17 export identities. Existing Adieu source changes were preserved.

Installed the built snapshot at 16:47 EDT with the game already closed. All four
root/bin-x64 DLL/PDB targets were backed up and hash-verified. No relaunch,
push, tag, prerelease or ZIP replacement. Receipt:
build/coo/homecoming-barrier-authority-20260920/installation.json.
DLL SHA256: 38C39521B160B7454664EBD75D5CAF6AB8BDC855C0CCB4B7D2BC72D256FE77CB.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-barrier-authority-20260920-164717.
Fresh in-game testing of both barriers remains necessary. This change makes no
new claim about the unresolved opening microstutter.

## Shield-release crash and Exodus continuation (2026-09-20)

The later failing run used DLL SHA256
775C713779E2DE565392D94B5105D35F6426F609D4D7CD10BF8F6447A401F0DE.
Device slots 56/57 reached position 1 at t946187, then the world thread stalled
before either barrier handoff returned. Three read-only thread-stack captures
are retained under build/coo/homecoming-shield-hang-20260920. Thread 64520 was
in the engine's fatal-exception wait, with the original fault at RVA 98D8C:
null RCX in an allocator virtual dispatch. The native call chain leads through
physics removal, named-state mutation A1FBB0, our poll_ship_barriers, and the
camera callback. The thread's actual TLS index 9 block had both +50 and +58
services equal to zero. This was not a device revision failure or Baboon.

The ownership repair exposed this unsafe call site: earlier permission rejection
prevented the state change from entering physics removal. The repair now runs
only after the existing native DF7FF0 device update, for that callback's exact
device/entity/placement. Camera polling no longer invokes it. Both native
allocator services and their callable methods must be available before any
ownership/state/presentation mutation; context is checked again after ownership
and before presentation. A missing context defers with a diagnostic, without
spoofing TLS, borrowing another thread's allocator, or suppressing exceptions.
Both doors remain separately revision/weak-reference/curve gated. Binding the
actual updating device also removes the full entity-table search.

The prior end-of-mission disconnect is separate: Homecoming selected lifetime 8,
which the roster encoder cannot encode, and stopped terminal publications. The
captured outro completion was followed by the 20-second lost-host timeout.
Homecoming now retains encodable success lifetime 6 and one-second terminal
publications while its completed outro queues the existing Exodus opening
(activity 288, mission_journey, bubble 3, slice 25). The private handoff requires
the exact completed outro/run and the current native session/launch nonce.
It uses ordinary destination selection/commit and confirmed native departure;
public mission-launch requests still require orbit. Loading-only suppression
expires on arrival or timeout; Exodus retains ownership of its vision movie.

Validation: Release x64 production build; Homecoming Debug/Release 166776 checks
each; launcher Debug/Release 689 each; ending packet suite 42458; Adieu 640;
28 package bindings; 28 isolated native authority/allocator checks. The new
allocator test reproduces the actual 98D8C fault with absent TLS+50 and verifies
normal native dispatch with a valid fixture. Native authority tests still stop
before full-world mutation. No live passability or campaign-handoff acceptance
is claimed without a new playthrough.

Installed at 17:37 EDT after preserving the failing log/stacks and closing the
diagnosed hung process with the user's authorization. All four root/bin-x64
DLL/PDB files were backed up and independently hash-verified; mission Lua files
already matched the workspace and were left untouched. No relaunch or release.
Receipt: build/coo/homecoming-shield-continuation-20260920/installation.json.
DLL SHA256: D05C20CD36108366CF7C9ADC8E593A619982C788BAE1AD520A90972DE2ABD724.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-shield-continuation-20260920-173707.

## Shield callback lifetime regression (2026-09-20, staged only)

The next run, PID 70252, used the verified 17:37 DLL. Read-only snapshots under
build/coo/homecoming-barrier-diagnosis-20260920 found both exact pod barriers
(slots 56/57) at actual and commanded position 1, acknowledged revision 2.
Both opening curves had finished their 3.5-second fade with zero remaining
time, output and writers, but both named-state providers remained blocking
(index 1). The host frame had both receipts; the callback's cached request was
still revisions 2/0 with empty component caches. The rounded 0.999 log sample
was not the cause. No native release or rejection had run.

DF7FF0 returns zero after device movement settles, before the longer opening
graph completes. Native scheduler 56D3CF calls 594CF0(disable=true) on a zero
callback return; nonzero retains its active phase. Moving the crash-prone
release out of the camera callback fixed the unsafe context but missed this
short device callback lifetime. The repaired hook preserves the native return
except when one of these two exact, locally owned, post-scan placements still
needs release. It holds that callback through delayed receipts, fade, and
guarded retries, then returns to native retirement after verified opening.
Cache contention also retains the exact pending callback instead of losing it.
Run/generation/placement/weak identity, authority, and allocator guards remain.
There is no broad entity scan, forced TLS, raw collision write or remote call.

Validation: Release x64 production build; Homecoming Debug/Release 167298 each;
1AU Release 1364 (including the production shared-hook retirement/quiescence
tests); launcher Release 689; ending packets 42458; Adieu 640; 28 package
bindings and 40 isolated native authority/allocator/scheduler checks. Updated
the older 1AU launcher expectation for the already-listed Exodus mission.
The scheduler test executes the real zero/nonzero branch, while the native
authority test still stops before world/physics mutation. Gameplay passability
requires a new run and is not claimed from these checks.

The reactor entrance reported during this investigation was a separate,
legitimate encounter gate: source 33 had a confirmed death, while source 35
actor 3FF42027 remained alive roughly 112 metres behind the player. Slot 59
had not been commanded open. The user resolved it; no reactor gate, kill
receipt, live memory, or installed file was changed.

Stage: build/coo/homecoming-barrier-tick-20260920. DLL SHA256:
6849C0BAAC6ECD109F761B0F9139B58DF51AA7556083B2CD1C5D123C447C344C.
The running game and the installed 17:37 build remain untouched. No release,
push, install or restart was performed for this staged build.

Subsequently installed at 18:18 EDT at the user's request, with the game already
closed. All four root/bin-x64 DLL/PDB targets and their backups were independently
hash-verified. Scripts were unchanged; no relaunch, live write, push or release.
Receipt: build/coo/homecoming-barrier-tick-20260920/installation.json.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-barrier-tick-20260920-181854.

## Reactor proximity and Exodus opening-mask ownership (2026-09-20, staged)

The user clarified that the reactor entrance is proximity-driven. This
supersedes the earlier description of its boss-clear dependency as a legitimate
gate. The generator graph now opens ship device 59 from pt_engine_room_lower,
without a boss-clear node. Boss spawns remain intact. The native open-pose
receipt still precedes chamber entry, and chamber entry still owns the turbine
targets and objective; the preceding room retains its existing visual preload.
The full-route regression holds the player back, admits both living bosses,
then verifies prompt opening on approach and completion without boss deaths.

A requested one-run memory bypass was prepared for PID 72428, run 1. Its
revalidation rejected the write because the player had already progressed.
The log independently records device 59 reaching position 1 at t971671, then
the mission completing and transitioning to Exodus. No live write occurred.

Exodus then showed a black screen for 76.344 seconds: opening_mask was asserted
at t1060656 and released at t1137000. Gameplay had already arrived at t1060671.
A read-only PDB-derived snapshot found a retained Homecoming controller with
owner 2/257, phase preparing, movie 0 and flyInComplete=true. Its absolute
deadline 101899000 maps exactly to the mask's release time. A teardown roster
had prepared Homecoming for the new run, and the shared fade adapter broadcast
Adieu's arrival to every opening controller. Homecoming's unplayed opening
therefore held the world fade until its 90-second preparation deadline.

The shared fade adapter now resolves the exact newest joined activity
incarnation and checks BOTH activity index and package before notifying or
querying an opening controller. A foreign retained controller cannot acquire
or reassert the mask. This excludes Homecoming from Exodus immediately; it
does not shorten legitimate cinematics, skip Adieu's intro, clear all fade
channels, or require the mission seed before the early C9 arrival. State locks
are released before mission-controller callbacks. Logs now identify the mask
owner instead of labelling every mission as Launchpad.

Validation: Release x64 DLL built; Homecoming Debug and Release 155836 checks
each (the count is lower because the removed boss wait no longer spends thirty
seconds in the simulation). New tests reproduce a retained same-run opening
mask, exclude it in Exodus before timeout, preserve all three valid opening
owners, and reject malformed/mismatched identities. Also passed: 1AU Release
1364, Adieu 640, launcher 689 and ending packets 42458.

Stage: build/coo/homecoming-proximity-exodus-fade-20260920.
DLL SHA256: BF599BE16493D6B6B50B67A8CC43228CBDA8639BE5C9C7D2A56B0BA33598395C.
The previous two-orange-barrier callback fix remains included. The installed
18:18 DLL and running game were not modified or restarted. New gameplay
verification is pending installation; no push, release or ZIP replacement.

Subsequently installed at 18:47 EDT after the user closed the game and requested
installation. Both root and bin/x64 DLL/PDB pairs were backed up and
hash-verified. Homecoming and Adieu Lua hashes matched in all three locations
and scripts were left unchanged. No relaunch, live write, push or release.
Receipt: build/coo/homecoming-proximity-exodus-fade-20260920/installation.json.
Backup: C:/Destiny 2 Development/.dawn/backups/homecoming-proximity-exodus-fade-20260920-184710.
Gameplay acceptance remains pending a new run.

## Opening-area periodic hitch: legacy pool probes (2026-09-21)

Read-only captures of PID 6212 (installed DLL SHA256
61DB06D52882B2376C70CB9949EF5BA3836927920495F009A5627B677093A537)
showed a roughly 125 ms hitch cadence while the opening's objects were loaded.
The same type-68 directive wrapper had roughly 31 ms pre/post log intervals
there, then mostly zero-tick intervals after those objects unloaded. Returning
loaded all five legacy probe targets again and restored the slow intervals even
with the later objective still active. The user withdrew the initial report
that returning to the opening stayed smooth. Logs have coarse tick resolution;
these measurements do not isolate the native apply from its diagnostic wrapper.

Normal builds now skip the old Tower Watch scene/spawner pool snapshots in both
the directive apply and periodic dialogue scan. These snapshots are diagnostic
only, invoke native lookup functions, and are not needed for mission receipts.
An explicit DAWN_ENABLE_TOWER_WATCH_SLOT_PROBES=1 diagnostic build can restore
them. The policy short-circuits before even qualifying the forced destination.
Native apply/scan forwarding, adventure feedback, opening dialogue handoff,
publication cadence, doors, actors, waves, and objective contents are unchanged.

Operation-count tests cover disabled probes across 1000 updates, opt-in probes,
and exclusion of other missions. Captures and build/test logs are retained in
build/coo/homecoming-fps-20260921. A fresh gameplay capture with the candidate
is required before calling the FPS improvement verified. No live patch is used.

Validation: Homecoming Debug and Release each passed 155840 checks; the Release
DLL built with zero warnings/errors. The legacy pool-probe trace is absent from
the binary; opening dialogue-handoff and directive traces remain. Candidate
SHA256: 28F6E7B8DF7E816002A000D92BDAB03CC14C1477F84F2AE2FCBC6A8D83654054.
Staged in build/coo/homecoming-slot-probe-fix-20260921, then installed while the
game was closed at 09:43 EDT. Root and bin/x64 DLL/PDB pairs were backed up and
hash-verified; no scripts/settings changed and no relaunch occurred. See that
stage's installation.json for the backup and file hashes. Gameplay performance
acceptance is still pending; disabling the suspect probe is not an A/B proof.

## Tower North bazaar door and Holliday's landing (1.7.3)

Report: after Ikora's Nova Bomb and "Board the command ship", the bazaar door in
Tower North stayed shut, so the mission could not continue. The recording shows
the same stall on three approaches, each with different enemies: an empty room in
front of the closed door, then Incendiors and Legionaries fighting at the door
after backtracking, then an Incendior standing in the door alcove.

Cause: the door's only open condition was a kill count. "Ikora's Cabal blasted"
waited for all four scripted Cabal (boulevard 19-22) to be admitted and confirmed
dead, but nothing guarantees that: a Cabal that survives the blast, stands behind
o_invisible_character_blocker (released only by the breach itself), or is
re-streamed after backtracking keeps the cohort open forever. Holliday's landing
had the same shape: it waited for all fifteen bazaar actors, including the
Incendiors (sq_flame) that retail brings through that door, which cannot reach
the player while it is shut.

Fix: two controller milestones replace the kill-only waits.
- bazaarDoor: Ikora's Cabal are confirmed dead, or the player has reached the
  door room (pt_bazaar, volume 77). The breach sequence, blocker release and
  native door-state handoff are unchanged.
- bazaarCrossed: the bazaar squads are dead, or the player has reached the far
  end of the bazaar (pt_bazaar_2, volume 88). The Hawk, Holliday's line and the
  pickup trigger are unchanged.
This matches the retail flow, where the door is blown open as the player arrives
and Holliday lands whether or not the Cabal are dead. Both still wait for Ikora's
Nova Bomb and the bazaar route triggers; enemy spawning and scenes are unchanged.

Verification: a second full native-receipt route runs with Ikora's fourth Cabal
and one bazaar Cabal alive for the rest of the mission. The door and the Hawk
must stay unrequested for 50 simulated seconds while the player holds short, then
follow the player's arrival. With the previous graph that route stalls at
"Ikora's Cabal blasted". Homecoming Debug and Release each passed 304432 checks.

Live check (2026-11-13): in a playthrough of the fix the bazaar door opened
when the player reached it. Holliday's landing and the pickup were not
separately confirmed.
