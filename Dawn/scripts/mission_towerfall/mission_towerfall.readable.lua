-- Homecoming v40: scene cast, population, interaction and presentation repairs.
local missions = require("missions")
local mission = require(missions.MISSION_TOWERFALL)
local campaign = require("lib.campaign")
local unit, line = campaign.unit, campaign.line

local function move(context, slots, transition)
for _, slot_id in ipairs(slots) do
local ok, message = pcall(function()
campaign.move(context, {slot_id}, transition)
end)

if not ok then
error(
"Homecoming device failed: "
.. tostring(slot_id)
.. " | action: "
.. tostring(transition)
.. " | "
.. tostring(message),
0
)
end
end
end
local Slot, Squad, Directive = mission.Slot, mission.Squad, mission.Directive
local cue = mission.DialogueCue.M_DIALOG_SENSOR_80B50913

local audio = (function()
local A = {}
local p = "hc28.audio."
local windows = {15601,4184,250,7978,2750,2750,9411,4902,11449,250,9112,2750,2750,2750,250,6717,2750,250,250,250,250,2750,7117,2750,4213,250,250,3117,4917,250,8784,250,250,250,4384,4384,4850,25423,6984,3531,4517,3050,250,10087,2750,250,2750,3149,2750,250,2750,4584,9384,2750,3850,4050,250,2750,2750,14488,2784,2784,3259,2757,5117,250,2750,2750,3879,2950,250,2750,11817,250,2931,3784,2750,6051,3317,5111,2917,4050,14954,4310,2760,3317,4398,2750,2750,250,3065,8054,7750,5250}
-- Queue length is bounded to 127 bytes. Pattern functions are unavailable in Sunrise.
local function head(q,first)
first=first or 1
for i=first,#q do
if string.byte(q,i)==44 then return tonumber(string.sub(q,first,i-1)),i+1 end
end
return nil,#q+1
end
local function contains(q,n)
local at=1
while at<=#q do
local value,next_at=head(q,at)
if value==n then return true end
at=next_at
end
return false
end
local function played(s,n)
return ((s:variable(p.."seen"..(n//32)) or 0) & (1 << (n%32))) ~= 0
end
local function pump(c,s)
if s:variable(p.."key") or s:variable(p.."busy") or s:timer(p.."retry") then return end
local q = s:variable(p.."queue") or ""
local n = head(q)
if not n then return end
local tries=(s:variable(p.."tries") or 0)+1
if tries>3 then return end
c:set_variable(p.."tries",tries)
local ok,key = pcall(function() return c:slot(Slot.M_DIALOG_SENSOR_80B50913):play_dialogue_cue{cue=n} end)
if not ok or not key or not key.value then
c:set_variable(p.."error",string.sub(tostring(key),1,127))
if tries<3 then c:start_timer(p.."retry",1000) end
return
end
c:set_variable(p.."current",n)
c:set_variable(p.."key",key.value)
end
function A.add(c,s,n)
if n == nil or played(s,n) then return end
local q = s:variable(p.."queue") or ""
if contains(q,n) then return end
if #q > 116 then c:set_variable(p.."error","Dialogue queue capacity reached");return end
c:set_variable(p.."queue",q..n..",")
pump(c,s)
end
function A.install(program)
local function chain(name,fn)
local previous=program[name]
program[name]=function(c,s,e) if previous then previous(c,s,e) end;fn(c,s,e or {}) end
end
chain("on_event_effect_result",function(c,s,e)
if not e.request_key or e.request_key.value~=s:variable(p.."key") then return end
c:clear_variable(p.."key")
if e.outcome~="transport_staged" then
c:set_variable(p.."error","Dialogue dispatch: "..tostring(e.outcome))
if (s:variable(p.."tries") or 0)<3 then c:start_timer(p.."retry",1000) end
return
end
c:clear_variable(p.."tries")
c:clear_variable(p.."error")
local n=s:variable(p.."current")
c:set_variable(p.."seen"..(n//32),(s:variable(p.."seen"..(n//32)) or 0) | (1 << (n%32)))
local q=s:variable(p.."queue") or ""
local _,next_at=head(q)
c:set_variable(p.."queue",string.sub(q,next_at))
c:set_variable(p.."busy",true)
c:start_timer(p.."window",windows[n+1] or 4000)
end)
chain("on_event_timer_elapsed",function(c,s,e)
if e.timer_name==p.."window" then c:clear_variable(p.."busy");pump(c,s) end
if e.timer_name==p.."retry" then pump(c,s) end
end)
chain("on_load",function(c,s)
if s:variable(p.."busy") and not s:timer(p.."window") then
c:start_timer(p.."window",windows[(s:variable(p.."current") or 0)+1] or 4000)
else pump(c,s) end
end)
local triggers={
[Slot.PT_CIV_RUN_B]={7}, [Slot.PT_WEAPON_COMPLETE]={28},
[Slot.PT_DIALOGUE_HANGAR_WINDOW]={35,37},
[Slot.PT_HANGAR_SPAWN]={35,37},
[Slot.PT_HANGAR_COMBAT]={35,37,38}, [Slot.PT_GOTO_PLAZA_80B50B91]={35,37,38,39},
[Slot.PT_PLAZA_ZAVALA_MEET]={50}, [Slot.PT_GOTO_BOULEVARD]={60,61},
[Slot.PT_GOTO_SPEAKER]={64},
}
chain("on_event_player_trigger",function(c,s,e)
local list=e.slot and triggers[e.slot.id]
for _,n in ipairs(list or {}) do A.add(c,s,n) end
end)
end
return A
end)()

local heroes = (function()
local H = {rows = {}}
local families = {"shaxx", "ikora", "zavala", "breach", "centurion"}
local function row(name, family, scene, cast, events, bindings)
local d = {name=name, family=family, scene=assert(mission.Scene[scene], scene), cast={}, ops={}}
for _, symbol in ipairs(cast) do
d.cast[#d.cast+1] = assert(Slot[symbol], symbol)
if Squad[symbol] then
d.ops[#d.ops+1] = {kind="reserve", id=Squad[symbol]}
end
end
for _, symbol in ipairs(bindings or {}) do
d.ops[#d.ops+1] = {kind="bind", id=assert(Slot[symbol], symbol)}
end
d.ops[#d.ops+1] = {kind="activate"}
for _, key in ipairs(events or {}) do d.ops[#d.ops+1] = {kind="event", key=key} end
H.rows[name] = d
end
row("shaxx", "shaxx", "SCENE_SHAXX", {"SQ_SHAXX", "O_SHAXX_DOOR_EXIT"}, {0x6F51AC66})
row("ikora", "ikora", "SCENE_IKORA_BOULEVARD", {"SQ_IKORA_80B5011F",
"SQUAD_CABAL_BLASTED_1", "SQUAD_CABAL_BLASTED_2", "SQUAD_CABAL_BLASTED_3",
"SQUAD_CABAL_BLASTED_4", "SQUAD_INVISIBLE_SHOOTING_TARGET"}, {0x6F51AC66})
H.rows.ikora.ops[6] = {kind="scene_source", id=Slot.SQUAD_INVISIBLE_SHOOTING_TARGET}
H.rows.ikora.ops[8] = {kind="event",key=0x84FFD4F6}
H.rows.ikora.ops[9] = {kind="entry_baseline"}
H.rows.ikora.ops[10] = {kind="entry_trigger"}
H.rows.ikora.ops[11] = {kind="event",key=0x6F51AC66}
H.rows.ikora.ops[12] = {kind="entry_cue"}
-- Reconstructed entry effect timing: native cue precedes Cabal blast.
-- The child graph already owns the later ship projectile; do not duplicate it.
H.rows.ikora.ops[13] = {kind="entry_projectile"}
H.rows.ikora.ops[14] = {kind="event",key=0x0B78A21A}
row("arrival", "zavala", "SC_ZAVALA", {"SQ_ZAVALA", "SQUAD_KILL_CABAL_5",
"SQUAD_KILL_CABAL_6", "SQUAD_KILL_CABAL_7", "SQUAD_KILL_CABAL_8"},
{0xB8C5C0A5, 0x1BED1ED3}, {"SQ_ZAVALA_BANSHEE"})
row("combat", "zavala", "SC_ZAVALA_COMBAT", {"SQ_ZAVALA", "SQUAD_KILL_CABAL_1",
"SQUAD_KILL_CABAL_2", "SQ_PLAZA_REINFORCE_A_A", "SQ_PLAZA_REINFORCE_A_B"})
row("ward", "zavala", "SC_ZAVALA_BUNKER_SHIELD_BUNKER",
{"SQ_ZAVALA", "O_MISSILE_SPAWNER"}, {0xC021F76C})
row("death", "zavala", "SC_ZAVALA_SHIELD_DEATH",
{"SQ_ZAVALA", "O_MISSILE_SPAWNER"}, {0xC021F76F})
row("revive", "zavala", "SC_ZAVALA", {"SQ_ZAVALA", "SQUAD_KILL_CABAL_5",
"SQUAD_KILL_CABAL_6", "SQUAD_KILL_CABAL_7", "SQUAD_KILL_CABAL_8"},
{0xB8C5C0A5}, {"SQ_ZAVALA_BANSHEE"})
row("breach", "breach", "SCENE_CABAL_FIRST_CONTACT",
{"SQUAD_FIRST_CONTACT_CABAL", "SQ_FRAME_DIE"}, {0xAE7CC69C})
row("centurion", "centurion", "SC_CENTURION_INTRO",
{"SQ_CENTURION_INTRO", "SQ_FRAME"}, {0x6F51AC66}, {"SQ_CENTURION_INTRO_CELL_1"})
for _, entry in ipairs({
{"breach", Slot.SQUAD_FIRST_CONTACT_CABAL, Slot.OBJ_CABAL_FIRST_CONTACT},
{"centurion", Slot.SQ_CENTURION_INTRO, Slot.OBJ_CENTURION_INTRO},
}) do
local ops = H.rows[entry[1]].ops
for index = #ops, 1, -1 do ops[index+1] = ops[index] end
ops[1] = {kind="objective", id=entry[2], objective=entry[3]}
end
table.insert(H.rows.breach.ops,5,{kind="entry_baseline"})
table.insert(H.rows.breach.ops,7,{kind="entry_cue"})
local function prefix(family) return "hc23." .. family .. "." end
local function fail(context, family, message)
local p = prefix(family)
context:set_variable(p .. "phase", "failed")
context:set_variable(p .. "error", string.sub(tostring(message), 1, 127))
context:cancel_timer(p .. "watchdog")
context:cancel_timer(p .. "observe")
end
local function request(context, family, run)
local ok, key = pcall(run)
if not ok or not key or not key.value then
fail(context, family, ok and "No request key returned" or key)
return
end
context:set_variable(prefix(family) .. "key", key.value)
end
local function release(c,s,f)
if f~="breach" and f~="centurion" then return end
local p=prefix(f)
if s:variable(p.."phase")~="staged" or (s:variable(p.."finished")~=true and s:variable(p.."ai_ready")~=true)
or s:variable(p.."ai_key") or s:variable(p.."ai_staged") then return end
local n=s:variable(p.."ai_tries") or 0
if n>=3 then return end
c:set_variable(p.."ai_tries",n+1)
local op=H.rows[f].ops[1]
local ok,k=pcall(function()return c:slot(op.id):assign_combat_objective{
objective=c:slot(op.objective),reserved=false,reconsider=true,refresh_player_awareness=true}end)
if ok and k and k.value then c:set_variable(p.."ai_key",k.value)
else
c:set_variable(p.."ai_error",string.sub(tostring(k),1,127))
if n<2 then c:start_timer(p.."ai_retry",1000) end
end
end
function H.finish(c,s,e)
if not e.slot then return end
for _,f in ipairs({"breach","centurion"}) do
local p=prefix(f)
local id=f=="breach" and Slot.SCENE_CABAL_FIRST_CONTACT or Slot.SC_CENTURION_INTRO
if e.slot.id==id and s:variable(p.."activation_token")~=nil
and e.activation_token==s:variable(p.."activation_token") then
c:set_variable(p.."finished",true)
release(c,s,f)
end
end
end
local pump
local function begin(context, state, family)
local p = prefix(family)
local name = state:variable(p .. "wanted")
context:set_variable(p .. "policy",37)
context:clear_variable(p .. "key")
context:clear_variable(p .. "activated")
context:clear_variable(p .. "activation_token")
context:clear_variable(p .. "cue_baseline")
context:clear_variable(p .. "ai_ready")
context:clear_variable(p .. "polls")
context:cancel_timer(p .. "observe")
context:clear_variable(p .. "finished")
context:clear_variable(p .. "error")
context:set_variable(p .. "name", name or "none")
if not name or name == "none" then
context:set_variable(p .. "phase", "stopped")
context:cancel_timer(p .. "watchdog")
return
end
context:set_variable(p .. "phase", "preparing")
context:set_variable(p .. "step", 1)
context:start_timer(p .. "watchdog", 60000)
pump(context, state, family)
end
pump = function(context, state, family)
local p = prefix(family)
local phase = state:variable(p .. "phase")
if phase == "failed" or state:variable(p .. "key") then return end
local name = state:variable(p .. "name")
if name ~= state:variable(p .. "wanted") then
if state:variable(p .. "activated") == true then
context:set_variable(p .. "phase", "stopping")
request(context, family, function() return context:scene(H.rows[name].scene):stop{} end)
else begin(context, state, family) end
return
end
if phase ~= "preparing" then return end
local d = H.rows[name]
local op = d.ops[state:variable(p .. "step")]
if not op then
context:set_variable(p .. "phase", "staged")
context:cancel_timer(p .. "watchdog")
if H.on_staged then H.on_staged(context, state, name) end
release(context,state,family)
return
end
if op.kind == "entry_trigger" then
if state:variable("hc30.ikora.entry") ~= true then context:cancel_timer(p .. "watchdog");return end
context:start_timer(p .. "watchdog",60000)
context:set_variable(p .. "step", state:variable(p .. "step")+1)
pump(context,state,family)
return
elseif op.kind == "entry_baseline" or op.kind == "entry_cue" then
local token=state:variable(p .. "activation_token")
if not token then fail(context,family,"Scene activation receipt has no token; paired runtime required");return end
local ok,value=pcall(function() return context:scene(d.scene):playback{activation_token=token} end)
if not ok then fail(context,family,value);return end
local ready=false
if value and value.activation_token==token then
local count=family=="breach" and value.actor_handoff_count or value.entry_cue_count
if op.kind=="entry_baseline" then
if count~=0 then fail(context,family,"Entry cue predates this activation baseline");return end
context:set_variable(p .. "cue_baseline",token)
ready=true
elseif state:variable(p .. "cue_baseline")==token
and count==1 and (family=="breach" or value.performer_bound==true) then ready=true end
end
if family=="breach" and op.kind=="entry_cue" and state:variable(p.."finished")==true then ready=true end
if ready then
if family=="breach" and op.kind=="entry_cue" then context:set_variable(p.."ai_ready",true) end
context:cancel_timer(p .. "observe")
context:set_variable(p .. "step",state:variable(p .. "step")+1)
pump(context,state,family)
elseif not state:timer(p .. "observe") then
local polls=(state:variable(p .. "polls") or 0)+1
context:set_variable(p .. "polls",polls)
if polls>240 then fail(context,family,"Native entry cue unavailable; no blast signal sent")
else context:start_timer(p .. "observe",250) end
end
return
end
request(context, family, function()
if op.kind == "objective" then
return context:slot(op.id):assign_combat_objective{objective=context:slot(op.objective)}
elseif op.kind == "entry_projectile" then
return context:slot(Slot.O_NOVA_BOMB_PROJECTILE):set_object_active{active=true}
elseif op.kind == "reserve" then
return context:squad(op.id):place{mode=context.sdk.squad_modes.reserve}
elseif op.kind == "scene_source" then
return context:scene(d.scene):reserve{source=context:slot(op.id)}
elseif op.kind == "bind" then
return context:slot(op.id):bind_combatant_to_squad()
elseif op.kind == "activate" then
return context:scene(d.scene):activate{spawn=false, cast=d.cast}
else return context:scene(d.scene):send_event{key=op.key} end
end)
end
function H.choose(context, state, name)
local d = assert(H.rows[name], name)
local p = prefix(d.family)
if state:variable(p .. "phase") == "failed" then return end
context:set_variable(p .. "wanted", name)
if not state:variable(p .. "name") then begin(context, state, d.family)
else pump(context, state, d.family) end
end
function H.stop(context, state, family)
local p = prefix(family)
if not state:variable(p .. "name") then return end
context:set_variable(p .. "wanted", "none")
pump(context, state, family)
end
function H.effect(context, state, event)
if not event.request_key or not event.request_key.value then return end
for _, family in ipairs(families) do
local p = prefix(family)
if state:variable(p.."ai_key")==event.request_key.value then
context:clear_variable(p.."ai_key")
if event.outcome=="transport_staged" then
context:set_variable(p.."ai_staged",true)
context:clear_variable(p.."ai_error")
context:cancel_timer(p.."ai_retry")
else
context:set_variable(p.."ai_error",string.sub(tostring(event.outcome),1,127))
if (state:variable(p.."ai_tries") or 0)<3 then context:start_timer(p.."ai_retry",1000) end
end
return
end
if state:variable(p .. "key") == event.request_key.value then
context:clear_variable(p .. "key")
if event.outcome ~= "transport_staged" then
fail(context, family, "Request " .. tostring(event.request_key.value) .. ": " .. tostring(event.outcome))
elseif state:variable(p .. "phase") == "stopping" then
begin(context, state, family)
elseif state:variable(p .. "phase") == "preparing" then
local step = state:variable(p .. "step")
local d = H.rows[state:variable(p .. "name")]
if d.ops[step].kind == "activate" then
context:set_variable(p .. "activated", true)
if event.activation_token then context:set_variable(p .. "activation_token",event.activation_token) end
end
if family=="breach" and d.ops[step].key==0xAE7CC69C and H.on_staged then
H.on_staged(context,state,"breach")
end
context:set_variable(p .. "step", step+1)
pump(context, state, family)
end
return
end
end
end
function H.load(context, state)
-- Translate retained v36 operation indices without replaying its pending blast.
local p=prefix("ikora")
if state:variable(p.."name") and state:variable(p.."policy")~=37 then
local step=state:variable(p.."step")
if step and step>=13 then context:set_variable(p.."step",step+1) end
context:set_variable(p.."policy",37)
end
context:set_variable("hc23.version", 23)
for _, family in ipairs(families) do
if state:variable(prefix(family) .. "name") then pump(context, state, family) end
release(context,state,family)
end
for _, entry in ipairs({{"shaxx", "hc13.opening.scene/SCENE_SHAXX"},
{"ikora", "hc13.ikora.played"}, {"zavala", "hc13.plaza.stage"}}) do
local old = state:variable(entry[2])
if (old == true or (type(old) == "number" and old > 0))
and not state:variable(prefix(entry[1]) .. "name") then
context:set_variable(prefix(entry[1]) .. "error",
"Legacy scene attempt; restart mission to use v23 cast setup")
end
end
end
function H.timer(context, state, event)
for _, family in ipairs(families) do
local p = prefix(family)
if event.timer_name==p.."observe" then pump(context,state,family);return end
if event.timer_name==p.."ai_retry" then release(context,state,family) end
if event.timer_name == p .. "watchdog" then
if family=="ikora" or family=="breach" then fail(context,family,"Native scene preparation/cue timed out; inspect cast and token");return end
context:set_variable(p .. "error", "Preparation pending; inspect phase, step and matched request receipts")
end
end
end
return H
end)()


local underwatch_cast = {
id = "cast",
squads = {
unit(Squad.SQ_RED_GUARD, Slot.SQ_RED_GUARD),
unit(Squad.SQ_RED_GUARD_ADS, Slot.SQ_RED_GUARD_ADS),
unit(Squad.SQ_FRAME_FAKE_FIGHT, Slot.SQ_FRAME_FAKE_FIGHT),
unit(Squad.SQ_RED_GUARD_FAKE_FIGHT, Slot.SQ_RED_GUARD_FAKE_FIGHT),
unit(Squad.SQ_CIVILIAN_CATATONIC, Slot.SQ_CIVILIAN_CATATONIC),
unit(Squad.SQ_CIVILIAN_HERO_MOMENT, Slot.SQ_CIVILIAN_HERO_MOMENT),
unit(Squad.SQ_CIVILIAN_KNEEL, Slot.SQ_CIVILIAN_KNEEL),
unit(Squad.SQ_CIVILIAN_STAND, Slot.SQ_CIVILIAN_STAND),
unit(Squad.SQ_CIVILIAN_WALL_SIT_A, Slot.SQ_CIVILIAN_WALL_SIT_A),
unit(Squad.SQ_CIVILIAN_WALL_SIT_B, Slot.SQ_CIVILIAN_WALL_SIT_B),
unit(Squad.SQ_CIVILIAN_WALL_SIT_C, Slot.SQ_CIVILIAN_WALL_SIT_C),
unit(Squad.SQ_CIVILIAN_GROUND_1, Slot.SQ_CIVILIAN_GROUND_1),
unit(Squad.SQ_CIVILIAN_GROUND_2, Slot.SQ_CIVILIAN_GROUND_2),
unit(Squad.SQ_CIVILIAN_ON_KNEES_CRYING, Slot.SQ_CIVILIAN_ON_KNEES_CRYING),
},
}

local first_contact = {
id = "contact",
trigger = Slot.PT_WALL_EXPLODE,
objective = Slot.OBJ_CABAL_FIRST_CONTACT,
squads = {
unit(Squad.SQUAD_FIRST_CONTACT_CABAL_BACKUP_A, Slot.SQUAD_FIRST_CONTACT_CABAL_BACKUP_A),
unit(Squad.SQUAD_FIRST_CONTACT_CABAL_BACKUP_B, Slot.SQUAD_FIRST_CONTACT_CABAL_BACKUP_B),
},
}

local centurion = {id="centurion",trigger=Slot.PT_CENTURION_INTRO_REINFORCE}

local wall = {
id = "wall",
trigger = Slot.PT_WALL_EXPLODE,
}

local post_gun = {
id = "post_gun",
trigger = Slot.PT_POSTGUN,
after = "gear",
objective = Slot.OBJ_POST_GUN,
squads = {
unit(Squad.SQ_FRAME_POST_GUN, Slot.SQ_FRAME_POST_GUN),
unit(Squad.SQ_RED_GUARD_POST_GUN, Slot.SQ_RED_GUARD_POST_GUN),
unit(Squad.SQ_RED_GUARD_POST_GUN_JUMP, Slot.SQ_RED_GUARD_POST_GUN_JUMP),
},
}

local hangar = {
id = "hangar_fight",
trigger = Slot.PT_HANGAR_SPAWN,
objective = Slot.OBJ_HANGAR,
squads = {
unit(Squad.SQ_HANGAR_OVERLOOK_A_A, Slot.SQ_HANGAR_OVERLOOK_A_A),
unit(Squad.SQ_HANGAR_OVERLOOK_A_A_CENT, Slot.SQ_HANGAR_OVERLOOK_A_A_CENT),
unit(Squad.SQ_HANGAR_OVERLOOK_A_B, Slot.SQ_HANGAR_OVERLOOK_A_B),
unit(Squad.SQ_HANGAR_OVERLOOK_A_C, Slot.SQ_HANGAR_OVERLOOK_A_C),
unit(Squad.SQ_HANGAR_OVERLOOK_B_A, Slot.SQ_HANGAR_OVERLOOK_B_A),
unit(Squad.SQ_FRIENDLIES_EARLY, Slot.SQ_FRIENDLIES_EARLY),
unit(Squad.SQ_FRIENDLIES_EARLY_UPPER, Slot.SQ_FRIENDLIES_EARLY_UPPER),
unit(Squad.SQ_RED_GUARD_FAKE_FIGHT_A, Slot.SQ_RED_GUARD_FAKE_FIGHT_A),
unit(Squad.SQ_RED_GUARD_FAKE_FIGHT_B, Slot.SQ_RED_GUARD_FAKE_FIGHT_B),
unit(Squad.SQ_RED_GUARD_FAKE_FIGHT_C, Slot.SQ_RED_GUARD_FAKE_FIGHT_C),
unit(Squad.SQ_FRAME_COVER_A, Slot.SQ_FRAME_COVER_A),
unit(Squad.SQ_FRAME_COVER_B, Slot.SQ_FRAME_COVER_B),
unit(Squad.SQ_FRAME_COVER_C, Slot.SQ_FRAME_COVER_C),
unit(Squad.SQ_FRAME_THROW_A, Slot.SQ_FRAME_THROW_A),
unit(Squad.SQ_RED_GUARD_THROW_A, Slot.SQ_RED_GUARD_THROW_A),
},
}


local bazaar = {id="bazaar",trigger=Slot.PT_BAZAAR,after="arrival",objective=Slot.OBJ_BAZAAR,squads={
unit(Squad.SQ_BAZAAR_START,Slot.SQ_BAZAAR_START),
unit(Squad.SQ_FLAME,Slot.SQ_FLAME),
unit(Squad.SQ_BAZAAR_TEASE_B,Slot.SQ_BAZAAR_TEASE_B),
}}

local program = campaign.new{
key = "towerfall",
directive_sensor = Slot.M_DIRECTIVE_SENSOR_80B50913,
dialogue_sensor = Slot.M_DIALOG_SENSOR_80B50913,
intro = {
{state = mission.states.STATE_80B500BC_0002_0001_80B500B1,
cinematic = Slot.PREFAB_HRO_CINEMATIC},
},
legs = {
{id = "underwatch", state = mission.states.STATE_80B500BC_0009_0000_80B500BB, arm = {
Slot.PT_START, Slot.PT_WEAPON, Slot.PT_PLAYER_NEAR_SHAXX, Slot.PT_WEAPON_COMPLETE,
Slot.PT_GOTO_MILITARY, Slot.PT_DROP_POD, Slot.PT_CENTURION_INTRO,
Slot.PT_WALL_EXPLODE, Slot.PT_POSTGUN,
Slot.PT_SHAXX_ENTERS, Slot.PT_SHAXX_ENTERS_BACKUP, Slot.PT_START_SHAXX_SCENE,
Slot.PT_CENTURION_INTRO_REINFORCE, Slot.PT_CIV_RUN_B, Slot.PT_HERO_MOMENT,
}},
{id = "military", state = mission.states.STATE_80B500BC_0004_0000_80B500B3, arm = {
Slot.PT_GOTO_PLAZA_80B50B91, Slot.PT_DIALOGUE_HANGAR_WINDOW, Slot.PT_HANGAR_SPAWN,
Slot.PT_HANGAR_COMBAT, Slot.PT_ARTIE_INTRO, Slot.PT_HANGAR_ARTIE,
}},
{id = "plaza", state = mission.states.STATE_80B500BC_0006_0000_80B500B6, arm = {
Slot.PT_GOTO_PLAZA_80B51058, Slot.PT_PLAZA_SPAWN_INIT, Slot.PT_PLAZA_ZAVALA_MEET,
Slot.PT_PLAZA_SPIRE, Slot.PT_SPIRE_TRIGGER, Slot.PT_ZAVALA_LOOP,
Slot.PT_DEFEND, Slot.PT_GOTO_BOULEVARD,
}},
{id = "boulevard", state = mission.states.STATE_80B500BC_0000_0000_80B500AD, arm = {
Slot.PT_START_CABAL_MOVEMENT, Slot.PT_START_IKORA, Slot.PT_GOTO_SPEAKER, Slot.PT_BAZAAR, Slot.PT_BAZAAR_MID, Slot.PT_BAZAAR_FAR, Slot.PT_BAZAAR_2,
}},
{id = "sky_battle", state = mission.states.STATE_80B500BC_0008_0000_80B500B8, arm = {
Slot.PT_SKYBATTLE_3, Slot.PT_DIALOG_ALMOST_THERE, Slot.PT_DESTROY_BATTLESHIP,
Slot.PT_GOTO_END, Slot.PT_DAMAGED, Slot.PT_DECK_START, Slot.PT_DECK_BOSS,
Slot.PT_MATRIX_DOOR, Slot.PT_ENGINE_ROOM_GHAUL,
}},
},
steps = {
{id = "home", directive = Directive.DEFEND_YOUR_HOME, navpoint = Slot.AP_IKORA,
ends = {trigger = Slot.PT_PLAYER_NEAR_SHAXX}},
{id = "gear", directive = Directive.GEAR_UP_FOR_THE_FIGHT,
navpoint = Slot.SLOT_0009_80B5168C,
on_start = function(context) move(context, {Slot.D_GUN_DOOR}, "open") end,
ends = {trigger = Slot.PT_WEAPON_COMPLETE}},
{id = "find", directive = Directive.FIND_ZAVALA_432D2C96,
navpoint = Slot.AP_GOTO_MILITARY_80B5168C,
ends = {trigger = Slot.PT_GOTO_MILITARY}},
{id = "hangar", directive = Directive.JOIN_ZAVALA_IN_THE_PLAZA,
navpoint = Slot.SLOT_0008_80B50B91,
ends = {trigger = Slot.PT_GOTO_PLAZA_80B50B91}},
{id = "zavala", directive = Directive.FIGHT_WITH_ZAVALA, navpoint = Slot.AP_PLAZA,
lines = {line(cue.CUE_40, Slot.SLOT_0019_80B51058)},
barrier = true, -- [PLAZA: no later trigger may skip the defence]
ends = {trigger = Slot.PT_GOTO_BOULEVARD}},
{id = "boulevard", directive = Directive.LEAVE_THE_PLAZA_AND_FIND_THE_SPEAKER,
navpoint = Slot.SLOT_000A_80B5097F,
lines = {line(cue.CUE_74, Slot.TV_BAZAAR_80B5097F)},
ends = {trigger = Slot.PT_GOTO_SPEAKER}},
{id = "board", directive = Directive.BOARD_THE_COMMAND_SHIP,
navpoint = Slot.SLOT_0004_80B5097F,
lines = {line(cue.CUE_75)},
barrier = true,
ends = {region = "sky_battle"}},
},
encounters = {
underwatch_cast, first_contact, centurion, wall, post_gun, hangar,
{id="hangar_floor_arrival",trigger=Slot.PT_HANGAR_COMBAT,objective=Slot.OBJ_HANGAR,
squads={
unit(Squad.SQ_HANGAR_A_A, Slot.SQ_HANGAR_A_A),
unit(Squad.SQ_HANGAR_A_A_FLANK, Slot.SQ_HANGAR_A_A_FLANK),
unit(Squad.SQ_HANGAR_A_B, Slot.SQ_HANGAR_A_B),
unit(Squad.SQ_HANGAR_A_B_SNIPER, Slot.SQ_HANGAR_A_B_SNIPER),
unit(Squad.SQ_HANGAR_FODDER_A, Slot.SQ_HANGAR_FODDER_A),
unit(Squad.SQ_HANGAR_FODDER_B, Slot.SQ_HANGAR_FODDER_B),
unit(Squad.SQ_HANGAR_FODDER_C, Slot.SQ_HANGAR_FODDER_C),
}},

{id = "underwatch_drop_pod", trigger = Slot.PT_DROP_POD,
objective = Slot.OBJ_CENTURION_INTRO,
squads = {unit(Squad.SQ_CENTURION_INTRO_BACKUP, Slot.SQ_CENTURION_INTRO_BACKUP)}},
{id = "underwatch_corridor_rush", trigger = Slot.PT_CENTURION_INTRO_REINFORCE,
objective = Slot.OBJ_CENTURION_INTRO,
squads = {unit(Squad.SQ_CENTURION_INTRO_RUSH, Slot.SQ_CENTURION_INTRO_RUSH)}},
{id = "underwatch_hero", trigger = Slot.PT_HERO_MOMENT,
objective = Slot.OBJ_CABAL_FIRST_CONTACT,
squads = {
unit(Squad.SQ_CABAL_HERO_MOMENT, Slot.SQ_CABAL_HERO_MOMENT),
unit(Squad.SQ_CABAL_HERO_MOMENT_B, Slot.SQ_CABAL_HERO_MOMENT_B),
}},
bazaar,
{id="bazaar_mid",trigger=Slot.PT_BAZAAR_MID,after="arrival",objective=Slot.OBJ_BAZAAR,squads={
unit(Squad.SQ_BAZAAR_TEASE_A,Slot.SQ_BAZAAR_TEASE_A),
unit(Squad.SQ_BAZAAR_A_B,Slot.SQ_BAZAAR_A_B),
}},
{id="bazaar_far",trigger=Slot.PT_BAZAAR_FAR,after="arrival",objective=Slot.OBJ_BAZAAR,squads={
unit(Squad.SQ_BAZAAR_A_A,Slot.SQ_BAZAAR_A_A),
}},
{id="bazaar_end",trigger=Slot.PT_BAZAAR_2,after="arrival",objective=Slot.OBJ_BAZAAR,squads={
unit(Squad.SQ_BAZAAR_A_C,Slot.SQ_BAZAAR_A_C),
unit(Squad.SQ_BAZAAR_FINALE,Slot.SQ_BAZAAR_FINALE),
}},
},
}


local function install_command_ship(program)
local p = "hc27.ship."
local ship = mission.states.STATE_80B500BC_0008_0000_80B500B8
local ending = mission.states.STATE_80B500BC_0001_0001_80B500AF
local cinema = Slot.OUTRO_CINEMATIC_CINEMATIC
local jobs, by_name, triggers, generators = {}, {}, {}, {}
local pump, evaluate, finish_job, observe_scan
local function bit(n) return 1 << (n - 1) end
local function has(s, field, n) return ((s:variable(p..field) or 0) & bit(n)) ~= 0 end
local function mark(c, s, field, n) c:set_variable(p..field,(s:variable(p..field) or 0)|bit(n)) end
local function unmark(c, s, field, n) c:set_variable(p..field,(s:variable(p..field) or 0)&~bit(n)) end
local function add(name, ops, trigger, first, last)
local job={name=name,ops=ops,index=#jobs+1,first=first or 1,last=last or 4}
assert(job.index<=62,"ship job mask capacity")
jobs[job.index],by_name[name]=job,job
if trigger then
if type(trigger)=="string" then trigger={trigger} end
for _,symbol in ipairs(trigger) do
triggers[#triggers+1]={slot=assert(Slot[symbol],symbol),job=job}
end
end
return job
end
local function op(ops,kind,id,value,wait) ops[#ops+1]={kind=kind,id=id,value=value,wait=wait} end
local function device(ops,symbol,value,wait) op(ops,"device",assert(Slot[symbol],symbol),value,wait) end
local function object(ops,symbol,active) op(ops,"object",assert(Slot[symbol],symbol),active) end
local function population(ops,symbols,objective)
for _,symbol in ipairs(symbols) do
if objective then op(ops,"objective",assert(Slot[symbol]),assert(Slot[objective])) end
op(ops,"squad",assert(Squad[symbol],symbol))
end
end
local function done(s,name) return has(s,"done",assert(by_name[name]).index) end
local function queue(c,s,name)
local job=assert(by_name[name],name)
if not has(s,"done",job.index) then mark(c,s,"pending",job.index) end
end
local function error_note(c,message) c:set_variable(p.."error",string.sub(tostring(message),1,127)) end
local function fail(c,s,message)
c:set_variable(p.."failed",true)
c:cancel_timer(p.."request")
error_note(c,message)
end
local function phase(s) return s:variable(p.."phase") or 0 end
local function here(s) return s:variable(p.."region")==ship.region_index end

local impact_groups={
{"START_A","START_B","START_C","START_D","START_E","START_F"},
{"HALL_A","HALL_B","HALL_C","HALL_D","HALL_E","HALL_F"},
{"HALL_B_A","HALL_B_B","HALL_B_C","HALL_B_D","HALL_B_E"},
{"DECK_A","DECK_A_B","DECK_A_C","DECK_B","DECK_B_B","DECK_B_C",
"DECK_C","DECK_C_B","DECK_C_C","DECK_C_D"},
}
local function impacts(ops,n,value)
for _,suffix in ipairs(impact_groups[n]) do
object(ops,"O_SHIELD_IMPACT_FX_"..suffix,true)
device(ops,"D_SHIELD_IMPACT_FX_"..suffix,value,false)
end
end
local function goal(ops,name,nav) op(ops,"goal",assert(Directive[name]),assert(Slot[nav])) end
local function say(ops,n) op(ops,"cue",nil,assert(mission.DialogueCue.M_DIALOG_SENSOR_80B50913["CUE_"..n])) end
local function scene(ops,name,stop) op(ops,stop and "scene_stop" or "scene",assert(mission.Scene[name])) end
local arrival,scan_done,overload,shutdown,departure={},{},{},{},{}
goal(arrival,"DISABLE_THE_SHIELDS","AP_LOCATE")
say(arrival,77)
op(arrival,"ghost",Slot.GL_CABAL_CONSOLE,true)
add("arrival",arrival)
op(scan_done,"ghost",Slot.GL_CABAL_CONSOLE,false)
device(scan_done,"D_CABAL_CONSOLE",1,false)
device(scan_done,"D_SHIP_POD_DOOR_A",1,true)
device(scan_done,"D_SHIP_POD_DOOR_B",1,true)
goal(scan_done,"REACH_THE_SHIELD_GENERATOR","AP_DESTROY_BATTLESHIP")
say(scan_done,79)
op(scan_done,"phase",nil,2)
add("scan_done",scan_done)
do
local ops={}
object(ops,"HAWK",true);device(ops,"D_HAWK",1,false)
add("hawk",ops,"PT_HAWK",1,2)
ops={}
object(ops,"DROP_POD_LAUNCH",true);device(ops,"D_DROP_POD_LAUNCH",1,false)
add("pod_launch",ops,"PT_DROP_POD_LAUNCH",1,2)
ops={}
population(ops,{"SQ_PODS"},"OBJ_DAMAGED");impacts(ops,1,.5)
add("pods_a",ops,"PT_PODS_DOOR_A",2,2)
ops={}
add("pods_b",ops,"PT_PODS_DOOR_B",2,2)
ops={};population(ops,{"SQ_DAMAGED"},"OBJ_DAMAGED")
add("damaged",ops,"PT_DAMAGED",2,2)
ops={}
population(ops,{"SQ_DAMAGED_HALL_FRONT","SQ_DAMAGED_HALL_REAR",
"SQ_DAMAGED_HALL_REAR_ANCHOR","SQ_DAMAGED_HALL_MELEE"},"OBJ_DAMAGED")
impacts(ops,2,.5);add("hall",ops,"PT_DAMAGED_HALL",2,2)
ops={}
population(ops,{"SQ_DAMAGED_HALL_REAR_STAIR","SQ_DAMAGED_HALL_REAR_STAIR_ANCHOR"},"OBJ_DAMAGED")
impacts(ops,3,.5);add("stairs",ops,"PT_DAMAGED_HALL_STAIRS",2,2)
ops={};device(ops,"D_SHIP_DOOR",1,false)
add("stairs_door",ops,"PT_DAMAGED_HALL_STAIRS_DOOR",2,2)
ops={}
population(ops,{"SQ_DECK_FRONT_A_A","SQ_DECK_FRONT_A_B","SQ_DECK_FRONT_A_C"},"OBJ_DECK")
impacts(ops,4,.5);add("deck_a",ops,"PT_DECK_START",2,2)
ops={}
population(ops,{"SQ_DECK_FRONT_B_A","SQ_DECK_FRONT_B_B","SQ_DECK_FRONT_B_C","SQ_DECK_FRONT_C_A"},"OBJ_DECK")
add("deck_b",ops,"PT_DECK_MID",2,2)
ops={}
population(ops,{"SQ_DECK_HARDPOINT_L","SQ_DECK_HARDPOINT_L2",
"SQ_DECK_HARDPOINT_L_LOWER","SQ_DECK_HARDPOINT_R"},"OBJ_DECK")
add("hardpoints",ops,"PT_DECK_HARDPOINT",2,2)
ops={}
population(ops,{"SQ_DECK_MINIBOSS"},"OBJ_DECK")
population(ops,{"SQ_DECK_ULTRA"},"OBJ_DECK_ULTRA")
op(ops,"sequence",Slot.SEQ_BOSS_ANNOUNCE)
add("boss",ops,"PT_DECK_BOSS",2,2)
ops={}
population(ops,{"SQ_SHIP_DOOR_MELEE","SQ_SHIP_DOOR_MELEE_B",
"SQ_SHIP_DOOR_MELEE_C","SQ_SHIP_DOOR_MELEE_D","SQ_SHIP_DOOR_MELEE_E"})
add("airlock",ops,"PT_AIRLOCK",2,2)
ops={}
population(ops,{"SQ_SHIP_ENTRY_A_A","SQ_SHIP_ENTRY_A_B","SQ_SHIP_ENTRY_A_B_O","SQ_SHIP_ENTRY_A_C"})
add("matrix",ops,"PT_MATRIX_DOOR",2,2)
ops={};population(ops,{"SQ_SHIELD_SNIPES","SQ_SHIELD_FRONT","SQ_SHIELD_MID"})
add("engine_upper",ops,"PT_ENGINE_ROOM_UPPER",2,2)
ops={};population(ops,{"SQ_SHIELD_REAR","SQ_SHIELD_MELEE"})
say(ops,84)
add("approach",ops,"PT_ENGINE_ROOM_LOWER",2,2)
ops={};device(ops,"D_SHIP_DOOR_ENTER",1,true)
op(ops,"flag",nil,"entry_open")
add("door_entry",ops)
end
goal(overload,"OVERLOAD_THE_GENERATOR","SLOT_000C_80B51397");say(overload,86)
for _,suffix in ipairs{"A","B","C"} do
generators[#generators+1]=assert(Slot["SHIELD_GENERATOR_"..suffix])
object(overload,"SHIELD_GENERATOR_"..suffix,true)
device(overload,"D_SHIELD_GENERATOR_"..suffix,.1,false)
device(overload,"D_GEN_"..suffix.."_LIGHTS",1,false)
device(overload,"D_VENTS_"..suffix,1,false)
end
for _,suffix in ipairs{"A","B"} do
object(overload,"O_SHIELD_GEN_"..suffix,true)
device(overload,"D_SHIELD_GEN_"..suffix,.1,false)
end
device(overload,"D_SHIELD_GEN_CORE",.1,false)
device(overload,"D_SHIELD_GEN_COLLAR",1,false)
device(overload,"D_HEAT_SINK_GLOWS",1,false)
population(overload,{"SQ_SHIELD_GEN_A","SQ_SHIELD_GEN_B","SQ_SHIELD_GEN_C"})
add("overload",overload,"PT_DESTROY_BATTLESHIP",2,2)
for i,suffix in ipairs{"A","B","C"} do
local ops={};device(ops,"D_GEN_"..suffix.."_LIGHTS",0,false)
add("kill_"..i,ops)
end
for _,symbol in ipairs{"D_SHIELD_GEN_A","D_SHIELD_GEN_B","D_SHIELD_GEN_CORE"} do
device(shutdown,symbol,.2,false)
end
device(shutdown,"D_SHIELD_GEN_COLLAR",0,false)
device(shutdown,"D_HEAT_SINK_GLOWS",0,false)
for _,symbol in ipairs{"ALARM","ALARM_LIGHT_1","ALARM_LIGHT_2","O_ESCAPE_CEILING_DUST"} do object(shutdown,symbol,true) end
device(shutdown,"D_GEN_EXIT_LIGHTS",1,false)
device(shutdown,"D_SHIP_DOOR_EXIT",1,true)
goal(shutdown,"ESCAPE_THE_COMMAND_SHIP","SLOT_0010_80B51397")
say(shutdown,91);say(shutdown,92)
population(shutdown,{"SQ_ESCAPE_A","SQ_ESCAPE_B"})
op(shutdown,"phase",nil,4)
add("shutdown",shutdown)
for i,suffix in ipairs{"A","B","C","D"} do
local ops={};scene(ops,"SC_EXPLOSION_"..suffix.."_80B508F4")
add("explosion_"..suffix,ops,"PT_ESCAPE_EXPLOSION_"..suffix..(i<=2 and "_80B508F4" or ""),4,4)
end
do
local ops={};scene(ops,"SC_EXPLOSION_FINALE_80B508F4")
add("finale",ops,"PT_GOTO_END",4,4)
add("exit_reached",{},"PT_GOTO_END_B",4,4)
end
for _,suffix in ipairs{"A","B","C","D","FINALE"} do
scene(departure,"SC_EXPLOSION_"..suffix.."_80B508F4",true)
departure[#departure].when=suffix=="FINALE" and "finale" or "explosion_"..suffix
end
op(departure,"ghost",Slot.GL_CABAL_CONSOLE,false)
op(departure,"goal_clear")
op(departure,"state",nil,ending)
add("depart",departure)
add("outro_start",{{kind="cinema",value=true}})
add("outro_stop",{{kind="cinema",value=false}})
add("complete",{{kind="complete"}})
for _,entry in ipairs(triggers) do
local target=entry.job.first==1 and arrival or entry.job.first==2 and scan_done or shutdown
if entry.job.name=="overload" then target=by_name.door_entry.ops end
op(target,"arm",entry.slot)
op(departure,"disarm",entry.slot)
table.insert(entry.job.ops,1,{kind="disarm",id=entry.slot})
end
for i,row in ipairs(departure) do
if row.kind=="state" then table.remove(departure,i);break end
end
op(departure,"state",nil,ending)

local function issue(c,row)
if row.kind=="device" then
return c:slot(row.id):set_channel{channel=c.sdk.device_channels.position,
value=c.sdk.unit(row.value),snap=false}
elseif row.kind=="object" then return c:slot(row.id):set_object_active{active=row.value}
elseif row.kind=="objective" then
return c:slot(row.id):assign_combat_objective{objective=c:slot(row.value)}
elseif row.kind=="squad" then return c:squad(row.id):place{}
elseif row.kind=="scene" then return c:scene(row.id):activate{spawn=false}
elseif row.kind=="scene_stop" then return c:scene(row.id):stop{}
elseif row.kind=="sequence" then return c:slot(row.id):play_sequence()
elseif row.kind=="ghost" then return c:slot(row.id):set_ghost_link{active=row.value}
elseif row.kind=="arm" then return c:slot(row.id):fire_trigger()
elseif row.kind=="disarm" then return c:slot(row.id):disarm_trigger()
elseif row.kind=="cue" then return c:slot(Slot.M_DIALOG_SENSOR_80B50913):play_dialogue_cue{cue=row.value}
elseif row.kind=="goal" then
return c:slot(Slot.M_DIRECTIVE_SENSOR_80B50913):set_directive{directive=row.id,navpoint=c:slot(row.value)}
elseif row.kind=="goal_clear" then return c:slot(Slot.M_DIRECTIVE_SENSOR_80B50913):clear_directives()
elseif row.kind=="state" then
c:set_variable(p.."phase",5);return c:select_state(row.value)
elseif row.kind=="cinema" then
if row.value then c:set_variable(p.."outro","starting");c:start_timer(p.."watchdog",180000) end
return c:slot(cinema):set_cinematic_active{active=row.value}
elseif row.kind=="complete" then
if c.mission_complete then return nil end
return c:complete_mission{}
end
error("Unknown ship operation "..tostring(row.kind))
end
local function current(s)
local job=jobs[s:variable(p.."job") or 0]
return job,job and job.ops[s:variable(p.."step") or 1]
end
local function advance(c,s)
c:clear_variable(p.."key")
c:clear_variable(p.."staged")
c:clear_variable(p.."applied")
c:clear_variable(p.."tries")
c:cancel_timer(p.."request")
c:set_variable(p.."step",(s:variable(p.."step") or 1)+1)
pump(c,s)
end
local function allowed(s,row)
if row.kind=="cinema" or row.kind=="complete" then return s:variable(p.."region")==ending.region_index end
return here(s)
end
local function request(c,s,row)
if not allowed(s,row) then return end
if row.kind=="object" and row.value==true then
for i,id in ipairs(generators) do
if row.id==id then
unmark(c,s,"present",i);unmark(c,s,"destroyed",i)
c:clear_variable(p.."gen_"..i);mark(c,s,"watch",i)
end
end
end
c:set_variable(p.."tries",(s:variable(p.."tries") or 0)+1)
local ok,key=pcall(function() return issue(c,row) end)
if not ok or not key or not key.value then
if row.kind=="complete" and c.mission_complete then advance(c,s);return end
fail(c,s,ok and "Ship request returned no key" or key)
return
end
c:set_variable(p.."key",key.value)
c:start_timer(p.."request",60000)
end
pump=function(c,s)
if s:variable(p.."failed")==true or s:variable(p.."key") then return end
if s:timer(p.."request") then return end
local job,row=current(s)
if not job then
for _,next_job in ipairs(jobs) do
if has(s,"pending",next_job.index) and not has(s,"done",next_job.index) then
job=next_job
c:set_variable(p.."job",job.index)
c:set_variable(p.."job_name",job.name)
c:set_variable(p.."step",1)
row=job.ops[1]
break
end
end
end
if not job then return end
if not row then
mark(c,s,"done",job.index)
unmark(c,s,"pending",job.index)
c:clear_variable(p.."job")
c:clear_variable(p.."step")
finish_job(c,s,job.name)
evaluate(c,s)
pump(c,s)
return
end
if row.when and not done(s,row.when) then advance(c,s);return end
if row.kind=="phase" or row.kind=="flag" then
if not here(s) then return end
if row.kind=="phase" then c:set_variable(p.."phase",row.value)
else c:set_variable(p..row.value,true) end
advance(c,s);return
end
request(c,s,row)
end
local function wait_applied(c,s,row)
if s:variable(p.."staged")~=true then return end
local ok,applied=pcall(function() return c:slot(row.id):applied{channel=c.sdk.device_channels.position} end)
if s:variable(p.."applied")==true or (ok and applied==true) then advance(c,s)
else c:start_timer(p.."request",3000) end
end
local function on_effect(c,s,e)
if not e.request_key or e.request_key.value~=s:variable(p.."key") then return end
local job,row=current(s)
if not job or not row then return end
if e.outcome~="transport_staged" then
if row.kind=="device" and (s:variable(p.."tries") or 0)<3 then
c:clear_variable(p.."key")
c:clear_variable(p.."staged")
c:clear_variable(p.."applied")
c:start_timer(p.."request",3000)
elseif row.kind=="device" and not row.wait then
error_note(c,job.name..": optional device "..tostring(e.outcome));advance(c,s)
else fail(c,s,job.name..": "..tostring(e.outcome)) end
return
end
c:set_variable(p.."staged",true)
if row.kind=="device" and row.wait then wait_applied(c,s,row) else advance(c,s) end
end
local function on_device(c,s,e)
-- Generator performer80C23AA6 couples vitality death to device position0.4;
-- its type4 source remains present. Only current watched/present generators count.
if here(s) and phase(s)>=2 and phase(s)<=3 and e.slot and not e.reset
and e.position_sequence and e.position_sequence>1 and e.position
and e.position>=.3999 and e.position<=.4001 then
for i,id in ipairs{Slot.D_SHIELD_GENERATOR_A,Slot.D_SHIELD_GENERATOR_B,Slot.D_SHIELD_GENERATOR_C} do
if e.slot.id==id and has(s,"watch",i) and has(s,"present",i) then
mark(c,s,"destroyed",i);evaluate(c,s);pump(c,s);break
end
end
end
local _,row=current(s)
if not row or row.kind~="device" or not row.wait or not e.slot or e.slot.id~=row.id then return end
local key=e:applied_request{channel=c.sdk.device_channels.position}
if key and key.value==s:variable(p.."key") then
c:set_variable(p.."applied",true)
wait_applied(c,s,row)
end
end

local function boss_clear(c)
return c:cohort{squads={Squad.SQ_DECK_MINIBOSS,Squad.SQ_DECK_ULTRA}}.cleared==true
end
evaluate=function(c,s)
if not here(s) or s:variable(p.."failed")==true then return end
if phase(s)==2 and done(s,"boss") and not done(s,"door_entry") then
if boss_clear(c) then
if s:variable(p.."boss_clear")~=true and not s:timer(p.."clear") then c:start_timer(p.."clear",3000) end
else c:cancel_timer(p.."clear");c:clear_variable(p.."boss_clear") end
if done(s,"approach") and s:variable(p.."boss_clear")==true then queue(c,s,"door_entry") end
elseif phase(s)==3 and done(s,"overload") then
for i=1,3 do if has(s,"destroyed",i) then queue(c,s,"kill_"..i) end end
if (s:variable(p.."destroyed") or 0)==7 then queue(c,s,"shutdown") end
elseif phase(s)==4 and done(s,"finale") then queue(c,s,"depart") end
end
finish_job=function(c,s,name)
if name=="arrival" then
local link=c:slot(Slot.GL_CABAL_CONSOLE):ghost_link()
if link then observe_scan(c,s,link) end
elseif name=="scan_done" then c:set_variable(p.."phase",2)
elseif name=="overload" then c:set_variable(p.."phase",3)
elseif name=="shutdown" then c:set_variable(p.."phase",4)
elseif name=="outro_stop" then c:set_variable(p.."outro","stopped");queue(c,s,"complete")
elseif name=="complete" then c:set_variable(p.."phase",6);c:cancel_timer(p.."watchdog") end
end

observe_scan=function(c,s,e)
if not here(s) or phase(s)~=1 or e.generation==nil then return end
if e.active==true and (e.progress or 0)>0 then
c:set_variable(p.."scan_generation",e.generation)
elseif e.active==false and done(s,"arrival") and e.generation==s:variable(p.."scan_generation") then
queue(c,s,"scan_done");pump(c,s)
end
end
local function on_object(c,s,e)
if not here(s) or not e.slot or e.present==nil or e.generation==nil then return end
for i,id in ipairs(generators) do
if e.slot.id==id and has(s,"watch",i) then
if e.present==true then
if s:variable(p.."gen_"..i)~=e.generation then unmark(c,s,"destroyed",i) end
c:set_variable(p.."gen_"..i,e.generation)
mark(c,s,"present",i)
elseif has(s,"present",i) and e.generation==s:variable(p.."gen_"..i) then mark(c,s,"destroyed",i) end
evaluate(c,s);pump(c,s)
return
end
end
end
local function on_trigger(c,s,e)
if not here(s) or not e.slot then return end
for _,entry in ipairs(triggers) do
if e.slot.id==entry.slot and phase(s)>=entry.job.first and phase(s)<=entry.job.last then
if entry.job.name=="overload" and s:variable(p.."entry_open")~=true then return end
if entry.job.name=="exit_reached" then queue(c,s,"finale") end
queue(c,s,entry.job.name);evaluate(c,s);pump(c,s)
return
end
end
end
local function on_timer(c,s,e)
if e.timer_name==p.."request" then
local job,row=current(s)
if not row or not allowed(s,row) then return end
if not s:variable(p.."key") then request(c,s,row)
elseif row.kind=="device" and row.wait and s:variable(p.."staged")==true then
local ok,applied=pcall(function() return c:slot(row.id):applied{channel=c.sdk.device_channels.position} end)
if s:variable(p.."applied")==true or (ok and applied==true) then advance(c,s)
else c:start_timer(p.."request",3000) end
else error_note(c,job.name..": request pending; no completion inferred") end
elseif e.timer_name==p.."watchdog" then
error_note(c,"Ship/outro pending; inspect phase, current request and actual client reports")
elseif e.timer_name==p.."clear" then
if here(s) and phase(s)==2 and done(s,"boss") and boss_clear(c) then
c:set_variable(p.."boss_clear",true);evaluate(c,s);pump(c,s)
end
end
end
local function arrive(c,s,region)
if region==nil then return end
c:set_variable(p.."region",region)
if region==ship.region_index then
if s:variable(p.."active")~=true then
c:set_variable(p.."active",true);c:set_variable(p.."owner",c.attempt_generation)
c:set_variable(p.."phase",1);queue(c,s,"arrival")
end
evaluate(c,s);pump(c,s)
local _,row=current(s)
if row and s:variable(p.."key") and not s:timer(p.."request") then
c:start_timer(p.."request",row.kind=="device" and 3000 or 60000)
end
elseif region==ending.region_index and phase(s)==5 then
queue(c,s,"outro_start");pump(c,s)
if s:variable(p.."key") and not s:timer(p.."request") then c:start_timer(p.."request",60000) end
else c:cancel_timer(p.."clear");c:clear_variable(p.."boss_clear") end
end
local function load(c,s)
if s:variable(p.."active")~=true then return end
-- Recover only the old exit-animation timeout, including late application having
-- advanced the cursor. Reissue that door and require a fresh applied receipt.
if here(s) and phase(s)==3 and s:variable(p.."failed")==true
and s:variable(p.."error")=="shutdown: device not applied" then
local job=current(s)
if job and job.name=="shutdown" then
for i,row in ipairs(job.ops) do
if row.kind=="device" and row.id==Slot.D_SHIP_DOOR_EXIT
and ((s:variable(p.."step") or 0)==i or s:variable(p.."step")==i+1) then
for _,k in ipairs{"failed","error","key","staged","applied","tries"} do c:clear_variable(p..k) end
c:cancel_timer(p.."request");c:set_variable(p.."step",i);break
end
end
end
end
arrive(c,s,s:variable(p.."region"))
if here(s) and phase(s)==1 and done(s,"arrival") then
local link=c:slot(Slot.GL_CABAL_CONSOLE):ghost_link()
if link then observe_scan(c,s,link) end
end
end
local function stop_outro(c,s,reason)
if phase(s)~=5 or s:variable(p.."region")~=ending.region_index then return end
local status=s:variable(p.."outro")
if status~="starting" and status~="playing" then return end
c:set_variable(p.."outro","stopping");c:set_variable(p.."ending",reason)
queue(c,s,"outro_stop");pump(c,s)
end
local callbacks={
{"on_start"},{"on_load",load},
{"on_event_region_changed",function(c,s,e) arrive(c,s,e.region_index) end},
{"on_event_client_state_changed",function(c,s,e)
if e.entered==true then arrive(c,s,e.held_region_index or e.current_region_index or e.region_index) end
end},
{"on_event_player_trigger",on_trigger},{"on_event_squad_state",function(c,s) evaluate(c,s);pump(c,s) end},
{"on_event_object_state",on_object},{"on_event_object_interacted"},
{"on_event_device_state",on_device},{"on_event_effect_result",on_effect},
{"on_event_ghost_link_state",function(c,s,e)
if e.slot and e.slot.id==Slot.GL_CABAL_CONSOLE then observe_scan(c,s,e) end
end},
{"on_event_timer_elapsed",on_timer},
{"on_event_cinematic_started",function(c,s,e)
if e.slot and e.slot.id==cinema and phase(s)==5 and s:variable(p.."outro")=="starting" then
c:set_variable(p.."outro","playing");c:cancel_timer(p.."watchdog")
end
end},
{"on_event_cinematic_terminated",function(c,s,e)
if not e.slot or e.slot.id~=cinema then return end
if s:variable(p.."outro")=="playing" then stop_outro(c,s,"terminated")
elseif s:variable(p.."outro")=="starting" then fail(c,s,"Outro ended before start; no completion inferred") end
end},
{"on_event_cinematic_skip_requested",function(c,s,e)
if e.slot and e.slot.id==cinema then stop_outro(c,s,"player_skip") end
end},
}
for _,entry in ipairs(callbacks) do
local base,handler=program[entry[1]],entry[2]
program[entry[1]]=function(c,s,e)
if e and e.attempt_generation and e.attempt_generation~=c.attempt_generation then return end
if s:variable(p.."owner") and s:variable(p.."owner")~=c.attempt_generation then
fail(c,s,"Checkpoint attempt changed; restart Homecoming to rebuild ship sources")
return
end
local region
if entry[1]=="on_event_region_changed" then region=e.region_index
elseif entry[1]=="on_event_client_state_changed" and e.entered==true then
region=e.held_region_index or e.current_region_index or e.region_index
end
if s:variable(p.."active")~=true and region~=ship.region_index then
if base then base(c,s,e) end
return
end
if handler then handler(c,s,e) end
end
end
end
install_command_ship(program)

local function install_homecoming_patch(program, mission, Slot, Squad, Directive)
local lib = require("lib.mission_lib")
local one, list = lib.one, lib.list
local Scene = mission.Scene -- Section A declares Slot, Squad and Directive only


local MILITARY_REGION =
one(mission.states.STATE_80B500BC_0004_0000_80B500B3, "military state").region_index
local PLAZA_STATE =
one(mission.states.STATE_80B500BC_0006_0000_80B500B6, "plaza state")
local PLAZA_REGION = PLAZA_STATE.region_index
local BOULEVARD_REGION =
one(mission.states.STATE_80B500BC_0000_0000_80B500AD, "boulevard state").region_index

local DEVICE_AMANDA_START = one(Slot.D_GATING_AMANDA_START, "d_gating_amanda_start")
local DEVICE_AMANDA_HANGAR = one(Slot.D_GATING_AMANDA_HANGAR, "d_gating_amanda_hangar")
local DEVICE_BOULEVARD_GATE = one(Slot.D_DOOR_GATING, "d_door_gating")
local DEVICE_BAZAAR_DOOR = one(Slot.D_DOOR_BAZAAR, "d_door_bazaar")
local SEQUENCE_BAZAAR_BREACH = one(Slot.SEQ_DOOR_EXPLOSION, "seq_door_explosion")

local TRIGGER_AMANDA_REACT = one(Slot.PT_AMANDA_REACT, "pt_amanda_react")
local TRIGGER_ARTIE_LOOKAT = one(Slot.PT_HANGAR_ARTIE_LOOKAT, "pt_hangar_artie_lookat")
local TRIGGER_AMANDA_SKIP = one(Slot.PT_AMANDA_SKIP, "pt_amanda_skip")
local TRIGGER_MOUNT_SHIP = one(Slot.PT_MOUNT_SHIP, "pt_mount_ship")
local TRIGGER_MILITARY_TO_PLAZA =
one(Slot.PT_GOTO_PLAZA_80B50B91, "military pt_goto_plaza")
local TRIGGER_BAZAAR_ENTRY = one(Slot.PT_BAZAAR, "pt_bazaar")             -- armed by the boulevard leg
local TRIGGER_BAZAAR_FARTHER = one(Slot.PT_BAZAAR_FARTHER, "pt_bazaar_farther")
local TRIGGER_PLAZA_GOTO = one(Slot.PT_GOTO_PLAZA_80B51058, "plaza pt_goto_plaza")
local TRIGGER_PLAZA_SPIRE = one(Slot.PT_PLAZA_SPIRE, "pt_plaza_spire")
local TRIGGER_SPIRE = one(Slot.PT_SPIRE_TRIGGER, "pt_spire_trigger")
local TRIGGER_PLAZA_INIT = one(Slot.PT_PLAZA_SPAWN_INIT, "pt_plaza_spawn_init")
local TRIGGER_ZAVALA_LOOP = one(Slot.PT_ZAVALA_LOOP, "pt_zavala_loop")
local TRIGGER_ZAVALA_MEET = one(Slot.PT_PLAZA_ZAVALA_MEET, "pt_plaza_zavala_meet") -- armed by the plaza leg
local TRIGGER_PLAZA_EXIT = one(Slot.PT_GOTO_BOULEVARD, "pt_goto_boulevard") -- armed by the plaza leg
local PLAZA_START_TRIGGERS = {
TRIGGER_PLAZA_GOTO,
TRIGGER_PLAZA_SPIRE,
TRIGGER_SPIRE,
TRIGGER_PLAZA_INIT,
TRIGGER_ZAVALA_LOOP,
}

local DIRECTIVE_SENSOR = one(Slot.M_DIRECTIVE_SENSOR_80B50913, "directive sensor")
local DIALOGUE_SENSOR = one(Slot.M_DIALOG_SENSOR_80B50913, "dialogue sensor")
local DIRECTIVE_DEFEND = one(Directive.DEFEND_THE_TOWER, "defend the tower")
local DIRECTIVE_LEAVE_PLAZA =
one(Directive.LEAVE_THE_PLAZA_AND_FIND_THE_SPEAKER_E68735ED, "leave the plaza")
local NAVPOINT_LEAVE_PLAZA = one(Slot.SLOT_000E_80B51058, "plaza exit marker")

local SQUAD_ZAVALA = one(Squad.SQ_ZAVALA, "zavala squad")
local SLOT_ZAVALA = one(Slot.SQ_ZAVALA, "zavala slot")
local OBJECTIVE_REINFORCE = one(Slot.OBJ_PLAZA_REINFORCE, "obj_plaza_reinforce")
local TASK_GROUP_REINFORCE =
one(mission.TaskGroup.OBJ_PLAZA_REINFORCE.GROUP_0, "obj_plaza_reinforce group 0")
local OBJECTIVE_PLAZA_KILL = one(Slot.OBJ_PLAZA_KILL_CABAL, "obj_plaza_kill_cabal")
local TASK_GROUP_PLAZA_KILL =
one(mission.TaskGroup.OBJ_PLAZA_KILL_CABAL.GROUP_0, "obj_plaza_kill_cabal group 0")

local SCENE_ZAVALA_COMBAT = one(Scene.SC_ZAVALA_COMBAT, "sc_zavala_combat")

local PROP_ZAVALA_GRENADE = one(Slot.O_ZAVALA_GRENADE_1, "o_zavala_grenade_1")
local PROP_ZAVALA_PINNED = one(Slot.OP_ZAVALA_PINNED, "op_zavala_pinned")
local PROP_BUBBLE_SHIELD = one(Slot.BUBBLE_SHIELD_1, "bubble_shield_1")
local PROP_ROCKET_KILLS_ZAVALA = one(Slot.O_ROCKET_KILLS_ZAVALA, "o_rocket_kills_zavala")
local PROP_DEAD_ZAVALA_INTERACT = one(Slot.O_DEAD_ZAVALA_INTERACT, "o_dead_zavala_interact")
local PROP_DEAD_ZAVALA_GHOST = one(Slot.O_DEAD_ZAVALA_GHOST, "o_dead_zavala_ghost")
local PROP_ORB_SPAWNER = one(Slot.O_ORB_SPAWNER, "o_orb_spawner")
local BARRAGE_PROPS = list(Slot.O_MISSILE_SPAWNER, Slot.O_MISSILE_SPAWNER_SCRIPT)
local DEVICE_MISSILES = one(Slot.D_MISSILE_SPAWNER_SCRIPT, "d_missile_spawner_script")
local DEVICE_DEAD_ZAVALA_GHOST = one(Slot.D_DEAD_ZAVALA_GHOST, "d_dead_zavala_ghost")
local HARD_WIPE_GLOBALS = one(Slot.HARD_WIPE_GLOBALS, "hard wipe globals")

local PLAZA_PROPS_OFF_AT_ARRIVAL = list(
Slot.BUBBLE_SHIELD_1, Slot.O_ROCKET_KILLS_ZAVALA, Slot.O_DEAD_ZAVALA_INTERACT,
Slot.O_DEAD_ZAVALA_GHOST, Slot.O_MISSILE_SPAWNER, Slot.O_MISSILE_SPAWNER_SCRIPT
)

local SKYBOX_SHIPS = list(
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_CABAL_SHIP,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_0, Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_1,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_2, Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_3,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_4, Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_5,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_6, Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_7,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_LEFT_0, Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_LEFT_1,
Slot.SPECOPS_PLAZA_SHIP_BATTLE_O_DO_SHIP_LEFT_2
)

local WAVE_CARRIERS = {
{objects = list(Slot.O_CABAL_CARRIER_L_80B5103A, Slot.O_CABAL_CARRIER_R_80B5103A),
devices = list(Slot.D_CABAL_CARRIER_L_80B5103A, Slot.D_CABAL_CARRIER_R_80B5103A)},
{objects = list(Slot.O_CABAL_CARRIER_B, Slot.O_CABAL_CARRIER_FAR_A,
Slot.O_CABAL_CARRIER_FAR_A_1, Slot.O_CABAL_CARRIER_FAR_A_2),
devices = list(Slot.D_CABAL_CARRIER_B, Slot.D_CABAL_CARRIER_FAR_A,
Slot.D_CABAL_CARRIER_FAR_A_1, Slot.D_CABAL_CARRIER_FAR_A_2)},
{objects = list(Slot.O_CABAL_CARRIER_FAR_B, Slot.O_CABAL_CARRIER_FAR_B_1,
Slot.O_CABAL_CARRIER_FAR_B_2, Slot.O_CABAL_CARRIER_FAR_C,
Slot.O_CABAL_CARRIER_FAR_C_1, Slot.O_CABAL_CARRIER_FAR_C_2),
devices = list(Slot.D_CABAL_CARRIER_FAR_B, Slot.D_CABAL_CARRIER_FAR_B_1,
Slot.D_CABAL_CARRIER_FAR_B_2, Slot.D_CABAL_CARRIER_FAR_C,
Slot.D_CABAL_CARRIER_FAR_C_1, Slot.D_CABAL_CARRIER_FAR_C_2)},
}

local CUE = mission.DialogueCue.M_DIALOG_SENSOR_80B50913
local PLAZA_CUES = {
wave = {{CUE.CUE_51, CUE.CUE_52}, {CUE.CUE_55}, {CUE.CUE_58}},
barrage_start = {CUE.CUE_53},
barrage_end = {CUE.CUE_54},
held = {},
leave = {CUE.CUE_59},
}
local AMANDA_START_CUE = nil  -- [REQUIRES IDENTIFIER] Amanda opens the hangar gate
local AMANDA_HANGAR_CUE = nil -- [REQUIRES IDENTIFIER] Amanda clears the hangar exit
local BAZAAR_BREACH_CUE = nil -- [REQUIRES IDENTIFIER] Ikora breaches the bazaar door

local MISSILES_OFF = "power_off"
local CARRIER_ARRIVE = "open"
local GHOST_SHOW, GHOST_HIDE = "open", "close"

local SPAWN_GRACE_MS = 20000           -- after this, a squad never seen alive is ignored
local SETTLE_MS = 3000                 -- a clear must hold this long before it counts
local MEET_FALLBACK_MS = 60000         -- start assault 1 this long after meeting Zavala
local PLAZA_SEED_RETRY_MS = 8000       -- retry if no opening Cabal ever report
local MAX_PLAZA_SEED_TRIES = 3
local ZAVALA_SPAWN_RETRY_MS = 8000
local MAX_ZAVALA_SPAWN_TRIES = 3
local BARRAGE_MS = 12000               -- Ward duration after assault 1
local PLAZA_INTERMISSION_MS = 5000     -- keeps dialogue and objectives from overlapping
local ZAVALA_SETTLE_MS = 3000          -- Zavala must stay down this long to count
local RETRY_DELAY_MS = 2000
local MAX_OPEN_TRIES = 5

local GATE_IDLE, GATE_REQUESTED, GATE_STAGED, GATE_OPEN, GATE_FAILED = 0, 1, 2, 3, 4

local PLAZA_NONE = 0
local PLAZA_ARRIVED = 1
local PLAZA_WAVE_1 = 2
local PLAZA_BARRAGE = 3
local PLAZA_ZAVALA_DOWN = 4
local PLAZA_WAVE_2 = 5
local PLAZA_WAVE_3 = 6
local PLAZA_HELD = 7

local VAR_PREFIX = "hc13."
local VAR_REGION = "hc13.region"
local VAR_ERROR = "hc13.error"
local VAR_WIPES = "hc13.wipes"
local VAR_WIPED = "hc13.wiped"
local ARMED_PREFIX = "hc13.armed/"
local GROUP_PREFIX = "hc13.g."
local TIMER_RETRY_PREFIX = "hc13.retry."
local TIMER_GRACE_PREFIX = "hc13.grace."
local TIMER_SETTLE_PREFIX = "hc13.settle."
local VAR_PLAZA_STAGE = "hc13.plaza.stage"
local VAR_PLAZA_HANDOFF = "hc13.plaza.handoff"
local VAR_PLAZA_MET = "hc13.plaza.met"
local VAR_PLAZA_SEEDED = "hc13.plaza.seeded"
local VAR_PLAZA_SEED_TRIES = "hc13.plaza.seed_tries"
local VAR_PLAZA_INIT_CLEAR = "hc13.plaza.init_clear"
local VAR_ZAVALA_SEEN = "hc13.zavala.seen"
local VAR_ZAVALA_SPAWN_TRIES = "hc13.zavala.spawn_tries"
local VAR_ZAVALA_ALIVE = "hc13.zavala.alive"
local VAR_ZAVALA_DOWN = "hc13.zavala.down"
local VAR_ZAVALA_COMBAT = "hc13.zavala.combat"
local VAR_NEXT_STAGE = "hc13.plaza.next_stage"
local TIMER_MEET_FALLBACK = "hc13.plaza.meet"
local TIMER_PLAZA_SEED_RETRY = "hc13.plaza.seed_retry"
local TIMER_BARRAGE = "hc13.plaza.barrage"
local TIMER_ZAVALA_SETTLE = "hc13.zavala.settle"
local TIMER_ZAVALA_SPAWN_RETRY = "hc13.zavala.spawn_retry"
local TIMER_NEXT_STAGE = "hc13.plaza.next"
local ERROR_TEXT_LIMIT = 127
local OUTCOME_STAGED = "transport_staged"

local EMPTY = {}
local REQUEST_LABELS = {}


local function note_error(context, where, message)
context:set_variable(VAR_ERROR,
string.sub(tostring(where) .. ": " .. tostring(message), 1, ERROR_TEXT_LIMIT))
end

local function guarded(context, where, request)
local ok, result = pcall(request)
if not ok then
note_error(context, where, result)
return nil
end
if result ~= nil then
local key_ok, key_value = pcall(function() return result.value end)
if key_ok and key_value ~= nil then
REQUEST_LABELS[key_value] = where
end
end
return result
end

local function contains(ids, id)
for _, value in ipairs(ids or EMPTY) do
if value == id then return true end
end
return false
end

local function arm(context, slot_id)
context:set_variable(ARMED_PREFIX .. slot_id, true)
guarded(context, "arm " .. slot_id, function()
context:slot(slot_id):fire_trigger()
end)
end

local function disarm(context, state, slot_id)
local name = ARMED_PREFIX .. slot_id
if state:variable(name) ~= true then return end
context:clear_variable(name)
guarded(context, "disarm " .. slot_id, function()
context:slot(slot_id):disarm_trigger()
end)
end

local function hold_trigger(context, slot_id)
context:clear_variable(ARMED_PREFIX .. slot_id)
guarded(context, "hold " .. slot_id, function()
context:slot(slot_id):disarm_trigger()
end)
end

local function play_cue(context, state, where, cue_number)
if cue_number == nil then return end
audio.add(context, state, cue_number)
end

local function play_cues(context, state, where, cues)
for _, cue_number in ipairs(cues or EMPTY) do
play_cue(context, state, where, cue_number)
end
end

local function show_goal(context, directive, navpoint)
guarded(context, "directive", function()
context:slot(DIRECTIVE_SENSOR):set_directive{
directive = directive,
navpoint = navpoint ~= nil and context:slot(navpoint) or nil,
}
end)
end

local function set_props(context, where, slots, active)
guarded(context, where, function()
context:activate_objects{slots = slots, active = active}
end)
end

local function move_devices(context, where, slots, transition_name)
for _, slot_id in ipairs(slots) do
guarded(context, where .. " " .. slot_id, function()
return context:slot(slot_id):transition{
transition = context.sdk.device_transitions[transition_name],
}
end)
end
end



local function members(...)
local result = {}
for _, key in ipairs(list(...)) do
result[#result + 1] = {squad = one(Squad[key], key), slot = one(Slot[key], key)}
end
return result
end

local GROUPS = {
{id = "hangar_start", region = MILITARY_REGION, strict = true, members = members(
"SQ_MILITARY_HALLWAY_DESTRUCTION", "SQ_HANGAR_OVERLOOK_A_A",
"SQ_HANGAR_OVERLOOK_A_A_CENT", "SQ_HANGAR_OVERLOOK_A_B", "SQ_HANGAR_OVERLOOK_A_C",
"SQ_HANGAR_OVERLOOK_B_A")},
{id = "hangar_floor", region = MILITARY_REGION, strict = true, members = members(
"SQ_HANGAR_OVERLOOK_B_B", "SQ_HANGAR_OVERLOOK_B_C", "SQ_HANGAR_OVERLOOK_SNIPER")},
{id = "bazaar", region = BOULEVARD_REGION, strict = true, members = members(
"SQUAD_CABAL_BLASTED_1", "SQUAD_CABAL_BLASTED_2",
"SQUAD_CABAL_BLASTED_3", "SQUAD_CABAL_BLASTED_4")},
{id = "plaza_init", region = PLAZA_REGION, population = true, members = members(
"SQUAD_KILL_CABAL_1", "SQUAD_KILL_CABAL_2", "SQUAD_KILL_CABAL_3", "SQUAD_KILL_CABAL_4",
"SQUAD_KILL_CABAL_5", "SQUAD_KILL_CABAL_6", "SQUAD_KILL_CABAL_7", "SQUAD_KILL_CABAL_8",
"SQUAD_CABAL_DROPOFF_1")},
{id = "wave_1", region = PLAZA_REGION, population = true, members = members(
"SQ_PLAZA_REINFORCE_START_A", "SQ_PLAZA_REINFORCE_START_B",
"SQ_PLAZA_INTERIM_A_A", "SQ_PLAZA_INTERIM_A_B")},
{id = "wave_2", region = PLAZA_REGION, population = true, members = members(
"SQ_PLAZA_REINFORCE_A_A", "SQ_PLAZA_REINFORCE_A_A_EXTRA", "SQ_PLAZA_REINFORCE_A_B",
"SQ_PLAZA_REINFORCE_A_C", "SQ_PLAZA_REINFORCE_A_D", "SQ_PLAZA_REINFORCE_A_D_EXTRA")},
{id = "wave_3", region = PLAZA_REGION, population = true, members = members(
"SQ_PLAZA_REINFORCE_B_A", "SQ_PLAZA_REINFORCE_B_A_EXTRA",
"SQ_PLAZA_REINFORCE_B_B", "SQ_PLAZA_REINFORCE_B_B_EXTRA")},
}

local GROUP_BY_ID = {}
local MEMBER_BY_SLOT = {}
for _, group in ipairs(GROUPS) do
GROUP_BY_ID[group.id] = group
for index, member in ipairs(group.members) do
MEMBER_BY_SLOT[member.slot] = {group = group, index = index}
end
end

local function group_var(group, field)
return (group.strict and "hc26.g." or GROUP_PREFIX) .. group.id .. "." .. field
end

local function scene_owns_plaza_source(squad)
for _, name in ipairs({"arrival", "combat"}) do
for _, op in ipairs(heroes.rows[name].ops) do
if op.kind == "reserve" and op.id == squad then return true end
end
end
return false
end

-- User-requested boundary: the lingering overlook fight must not gate this door.
-- Keep source indices stable for saved attempts. Source17 is excluded only when
-- its population was deliberately moved before any corridor placement.
local function military_member_required(state,group,index)
if group.id=="hangar_start" and index==4 then return false end
if group.id=="hangar_floor" and index==2 and state:variable("hc38.military.relocated")==true then return false end
return true
end
local function group_status(context, state, group)
if group.population then
local squads = {}
for _, member in ipairs(group.members) do
if not scene_owns_plaza_source(member.squad) then squads[#squads+1] = member.squad end
end
local cohort = context:cohort{squads=squads}
return cohort.cleared, cohort.observed_full and #squads or 0, cohort.alive_count or -1
end
local graced = state:variable(group_var(group, "graced")) == true
local seen, alive, all_seen = 0, 0, true
for index = 1, #group.members do
if military_member_required(state,group,index) then
if state:variable(group_var(group, "s" .. index)) == true then
seen = seen + 1
alive = alive + (state:variable(group_var(group, "a" .. index)) or 0)
else
all_seen = false
end
end
end
if group.strict then
local pending=""
for index,member in ipairs(group.members) do
if military_member_required(state,group,index) then
if state:variable(group_var(group,"s"..index))~=true then pending="Unobserved "..member.slot;break
elseif (state:variable(group_var(group,"a"..index)) or 0)>0 then pending="Alive "..member.slot;break end
end
end
if pending~="" then context:set_variable("hc36.wait."..group.id,pending)
else context:clear_variable("hc36.wait."..group.id) end
return alive == 0 and all_seen, seen, alive end
return alive == 0 and (all_seen or graced), seen, alive
end

local function group_evaluate(context, state, group)
if state:variable(group_var(group, "started")) ~= true then return end
if state:variable(group_var(group, "done")) == true then return end
local clear = group_status(context, state, group)
local settling = state:variable(group_var(group, "settling")) == true
if clear and not settling then
context:set_variable(group_var(group, "settling"), true)
context:start_timer(TIMER_SETTLE_PREFIX .. group.id, SETTLE_MS)
elseif not clear and settling then
context:clear_variable(group_var(group, "settling"))
context:cancel_timer(TIMER_SETTLE_PREFIX .. group.id)
end
end

local function group_start(context, state, group_id)
local group = GROUP_BY_ID[group_id]
if state:variable(group_var(group, "started")) == true then return end
context:set_variable(group_var(group, "started"), true)
if not group.strict and not group.population then context:start_timer(TIMER_GRACE_PREFIX .. group.id, SPAWN_GRACE_MS) end
group_evaluate(context, state, group)
end

local function group_done(state, group_id)
return state:variable(group_var(GROUP_BY_ID[group_id], "done")) == true
end

local function group_record(context, state, event)
local slot = event.slot
if slot == nil or event.alive_count == nil then return end
local entry = MEMBER_BY_SLOT[slot.id]
if entry == nil then return end
local group, index = entry.group, entry.index
if group.population then group_evaluate(context, state, group); return end
context:set_variable(group_var(group, "a" .. index), event.alive_count)
if not group.strict or event.alive_count > 0 then
context:set_variable(group_var(group, "s" .. index), true)
end
group_evaluate(context, state, group)
end


local function stage_amanda_start(context, state)
play_cue(context, state, "amanda_start cue", AMANDA_START_CUE)
end

local function stage_amanda_hangar(context, state)
play_cue(context, state, "amanda_hangar cue", AMANDA_HANGAR_CUE)
end

local function stage_boulevard_gate(context, state)
end

local function stage_bazaar_breach(context, state)
guarded(context, "bazaar breach sequence", function()
context:slot(SEQUENCE_BAZAAR_BREACH):play_sequence()
end)
play_cue(context, state, "bazaar breach cue", BAZAAR_BREACH_CUE)
end

local GATES = {
{
id = "amanda_start",
device = DEVICE_AMANDA_START,
region = MILITARY_REGION,
arm = {TRIGGER_AMANDA_REACT, TRIGGER_ARTIE_LOOKAT, TRIGGER_AMANDA_SKIP},
group = "hangar_start",
reset_position = true,
on_open = stage_amanda_start,
},
{
id = "amanda_hangar",
device = DEVICE_AMANDA_HANGAR,
region = MILITARY_REGION,
arm = {TRIGGER_MOUNT_SHIP},
group = "hangar_floor",
reset_position = true,
on_open = stage_amanda_hangar,
},
{
id = "boulevard_gate",
device = DEVICE_BOULEVARD_GATE,
region = BOULEVARD_REGION,
open_on_region = true,
on_open = stage_boulevard_gate,
},
{
id = "bazaar_door",
device = DEVICE_BAZAAR_DOOR,
region = BOULEVARD_REGION,
arm = {TRIGGER_BAZAAR_FARTHER},
group = "bazaar",
on_open = stage_bazaar_breach,
},
}


local function gate_var(gate, field)
return VAR_PREFIX .. gate.id .. "." .. field
end

local function gate_state(state, gate)
return state:variable(gate_var(gate, "state")) or GATE_IDLE
end

local function start_watch(context, state, gate)
if gate.group ~= nil then group_start(context, state, gate.group) end
end

local function request_close(context, state, gate)
-- Retry an unconfirmed close with a fresh request, within the existing attempt limit.
local tries = (state:variable(gate_var(gate, "close_tries")) or 0) + 1
if tries > MAX_OPEN_TRIES then
context:set_variable(gate_var(gate, "state"), GATE_FAILED)
note_error(context, gate.id, "close not applied")
return
end
context:set_variable(gate_var(gate, "close_tries"), tries)
local key = guarded(context, gate.id .. " close", function()
return context:slot(gate.device):set_channel{
channel = context.sdk.device_channels.position,
value = context.sdk.unit(0), snap = true,
}
end)
if key then context:set_variable(gate_var(gate, "close_key"), key.value) end
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
end

local function request_open(context, state, gate)
local tries = (state:variable(gate_var(gate, "tries")) or 0) + 1
context:set_variable(gate_var(gate, "tries"), tries)
local key = guarded(context, gate.id .. " open", function()
if gate.reset_position then
return context:slot(gate.device):set_channel{
channel = context.sdk.device_channels.position,
value = context.sdk.unit(1), snap = false,
}
end
return context:slot(gate.device):transition{
transition = context.sdk.device_transitions.open,
snap = tries > 1,
}
end)
if key == nil then
context:set_variable(gate_var(gate, "state"), GATE_FAILED)
return
end
context:set_variable(gate_var(gate, "key"), key.value)
context:set_variable(gate_var(gate, "state"), GATE_REQUESTED)
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
end

local function open_gate(context, state, gate, reason)
if gate_state(state, gate) ~= GATE_IDLE then return end
if state:variable(VAR_REGION) ~= gate.region then return end
if gate.group and not group_done(state, gate.group) then return end
if gate.reset_position and state:variable(gate_var(gate, "closed")) ~= true then return end
context:cancel_timer(TIMER_RETRY_PREFIX .. gate.id)
for _, slot_id in ipairs(gate.arm or EMPTY) do
disarm(context, state, slot_id)
end
context:set_variable(gate_var(gate, "reason"), reason)
if gate.on_open ~= nil then gate.on_open(context, state) end
request_open(context, state, gate)
end

local function confirm_close(context, state, gate)
if state:variable(gate_var(gate, "closed")) == true then return true end
if state:variable(gate_var(gate, "close_key")) == nil then return false end
local applied = guarded(context, gate.id .. " close applied", function()
return context:slot(gate.device):applied{channel = context.sdk.device_channels.position}
end)
if applied ~= true then return false end
context:set_variable(gate_var(gate, "closed"), true)
if gate_state(state, gate) == GATE_FAILED then
context:set_variable(gate_var(gate, "state"), GATE_IDLE)
end
context:cancel_timer(TIMER_RETRY_PREFIX .. gate.id)
open_gate(context, state, gate, "cleared after close confirmed")
return true
end

local function gates_enter_region(context, state, region)
if state:variable("hc26.gates") ~= true then
for _, id in ipairs({"hangar_start", "hangar_floor", "bazaar"}) do
if state:variable(GROUP_PREFIX .. id .. ".started") == true then
note_error(context, "v26 gates", "Start a fresh mission; old combat groups cannot be migrated")
end
end
context:set_variable("hc26.gates", true)
end
for _, gate in ipairs(GATES) do
if gate.region == region and gate_state(state, gate) == GATE_IDLE then
start_watch(context, state, gate)
if gate.reset_position and not confirm_close(context, state, gate) then
if state:variable(gate_var(gate, "close_key")) == nil
and state:timer(TIMER_RETRY_PREFIX .. gate.id) == nil then
request_close(context, state, gate)
elseif state:timer(TIMER_RETRY_PREFIX .. gate.id) == nil then
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
end
end
if gate.open_on_region then
open_gate(context, state, gate, "region")
elseif gate_state(state, gate) == GATE_IDLE then
if state:variable(gate_var(gate, "armed")) ~= true then
for _, slot_id in ipairs(gate.arm or EMPTY) do arm(context, slot_id) end
context:set_variable(gate_var(gate, "armed"), true)
end
if gate.group and group_done(state, gate.group) then
open_gate(context, state, gate, "cleared on arrival")
end
end
end
end
end

local function gates_confirm_pending(context, state)
for _, gate in ipairs(GATES) do
local current = gate_state(state, gate)
if gate.region == state:variable(VAR_REGION) and current == GATE_FAILED
and gate.reset_position and state:variable(gate_var(gate, "closed")) ~= true
and state:variable(gate_var(gate, "key")) == nil then
confirm_close(context, state, gate)
elseif gate.region == state:variable(VAR_REGION)
and (current == GATE_REQUESTED or current == GATE_STAGED or current == GATE_FAILED) then
local applied = guarded(context, gate.id .. " applied", function()
return context:slot(gate.device):applied{
channel = context.sdk.device_channels.position,
}
end)
if applied == true and state:variable(gate_var(gate, "key")) ~= nil then
context:set_variable(gate_var(gate, "state"), GATE_OPEN)
context:cancel_timer(TIMER_RETRY_PREFIX .. gate.id)
elseif current ~= GATE_FAILED and state:timer(TIMER_RETRY_PREFIX .. gate.id) == nil then
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
end
end
end
end

local function gates_group_cleared(context, state, group_id)
for _, gate in ipairs(GATES) do
if gate.group == group_id then
open_gate(context, state, gate, "cleared")
end
end
end


local WAVE_GROUPS = {"wave_1", "wave_2", "wave_3"}
local WAVE_STAGES = {PLAZA_WAVE_1, PLAZA_WAVE_2, PLAZA_WAVE_3}

local function plaza_stage(state)
return state:variable(VAR_PLAZA_STAGE) or PLAZA_NONE
end

local function plaza_show_goal(context, state)
local stage = plaza_stage(state)
if stage == PLAZA_HELD then
show_goal(context, DIRECTIVE_LEAVE_PLAZA, NAVPOINT_LEAVE_PLAZA)
elseif stage >= PLAZA_ARRIVED then
show_goal(context, DIRECTIVE_DEFEND, nil)
end
end

local function start_zavala_combat(context, state)
context:set_variable("hc23.zavala.combat_pending", true)
local name = state:variable("hc23.zavala.name")
if name == "combat" then return end
if (state:variable(VAR_ZAVALA_ALIVE) or 0) <= 0
or state:variable("hc23.zavala.phase") ~= "staged"
or (name ~= "arrival" and name ~= "revive") then return end
context:clear_variable("hc23.zavala.combat_pending")
heroes.choose(context, state, "combat")
end

local function stop_zavala_combat(context, state)
context:clear_variable(VAR_ZAVALA_COMBAT)
context:clear_variable("hc23.zavala.combat_pending")
heroes.stop(context, state, "zavala")
end

local function disable_darkness(context)
guarded(context, "plaza respawn enabled", function()
return context:slot(HARD_WIPE_GLOBALS):set_darkness_zone{enabled = false}
end)
end

local function spawn_zavala_arrival(context, state)
if state:variable("hc23.zavala.name") then return end
heroes.choose(context, state, "arrival")
end


local function stage_plaza_arrival(context, state)
context:set_variable(VAR_PLAZA_STAGE, PLAZA_ARRIVED)
context:set_variable("hc13.plaza.assault", 1)
context:set_variable("hc13.plaza.assaults_defended", 0)
set_props(context, "plaza props off", PLAZA_PROPS_OFF_AT_ARRIVAL, false)
set_props(context, "zavala pinned", {PROP_ZAVALA_PINNED}, true)
set_props(context, "skybox ships", SKYBOX_SHIPS, true)
disable_darkness(context)
spawn_zavala_arrival(context, state)
show_goal(context, DIRECTIVE_DEFEND, nil)
context:start_timer(TIMER_MEET_FALLBACK, MEET_FALLBACK_MS)
end

local function seed_plaza_initial(context, state, retry)
if retry ~= true and state:variable(VAR_PLAZA_SEEDED) == true then return end
local tries = (state:variable(VAR_PLAZA_SEED_TRIES) or 0) + 1
context:set_variable(VAR_PLAZA_SEED_TRIES, tries)
local group = GROUP_BY_ID["plaza_init"]
for _, member in ipairs(group.members) do
guarded(context, "plaza objective " .. member.slot, function()
return context:slot(member.slot):assign_combat_objective{
objective = context:slot(OBJECTIVE_PLAZA_KILL),
task_group = TASK_GROUP_PLAZA_KILL,
}
end)
if not scene_owns_plaza_source(member.squad) then
guarded(context, "plaza place " .. member.slot, function()
return context:squad(member.squad):place{mode = context.sdk.squad_modes.replace}
end)
end
end
context:set_variable(VAR_PLAZA_SEEDED, true)
group_start(context, state, group.id)
if not group.population then context:start_timer(TIMER_PLAZA_SEED_RETRY, PLAZA_SEED_RETRY_MS) end
end

local function start_plaza(context, state)
if plaza_stage(state) == PLAZA_NONE then
stage_plaza_arrival(context, state)
end
hold_trigger(context, TRIGGER_PLAZA_EXIT)
seed_plaza_initial(context, state, false)
end

local function queue_plaza_stage(context, state, next_stage)
if state:variable(VAR_NEXT_STAGE) ~= nil then return end
context:set_variable(VAR_NEXT_STAGE, next_stage)
context:start_timer(TIMER_NEXT_STAGE, PLAZA_INTERMISSION_MS)
end

local PLAZA_WAVE_COUNTS = {
[Squad.SQ_PLAZA_REINFORCE_START_B]=3,
[Squad.SQ_PLAZA_INTERIM_A_A]=3, [Squad.SQ_PLAZA_INTERIM_A_B]=3,
[Squad.SQ_PLAZA_REINFORCE_A_A_EXTRA]=4,
[Squad.SQ_PLAZA_REINFORCE_A_D]=3, [Squad.SQ_PLAZA_REINFORCE_A_D_EXTRA]=3,
[Squad.SQ_PLAZA_REINFORCE_B_A]=3, [Squad.SQ_PLAZA_REINFORCE_B_A_EXTRA]=3,
[Squad.SQ_PLAZA_REINFORCE_B_B]=3, [Squad.SQ_PLAZA_REINFORCE_B_B_EXTRA]=3,
}
local function stage_plaza_wave(context, state, n)
if n == 2 and state:variable("hc13.zavala.revived") ~= true then return end
if state:variable(VAR_ZAVALA_DOWN) == true then return end
context:clear_variable(VAR_NEXT_STAGE)
context:set_variable(VAR_PLAZA_STAGE, WAVE_STAGES[n])
context:set_variable("hc13.plaza.assault", n) -- read "1", "2", "3" of 3 on Mission state
context:set_variable("hc13.plaza.assaults_defended", n - 1)
set_props(context, "bubble between waves off", {PROP_BUBBLE_SHIELD}, false)
start_zavala_combat(context, state)
show_goal(context, DIRECTIVE_DEFEND, nil)
play_cues(context, state, "wave cue", PLAZA_CUES.wave[n])
if n == 1 then
set_props(context, "orb spawner", {PROP_ORB_SPAWNER}, true)
end
local carriers = WAVE_CARRIERS[n]
set_props(context, "carriers", carriers.objects, true)
move_devices(context, "carrier", carriers.devices, CARRIER_ARRIVE)
local group = GROUP_BY_ID[WAVE_GROUPS[n]]
for _, member in ipairs(group.members) do
guarded(context, "wave objective " .. member.slot, function()
return context:slot(member.slot):assign_combat_objective{
objective = context:slot(OBJECTIVE_REINFORCE),
task_group = TASK_GROUP_REINFORCE,
}
end)
if not scene_owns_plaza_source(member.squad) then
guarded(context, "wave place " .. member.slot, function()
local squad = context:squad(member.squad)
local counts = squad:counts()
if PLAZA_WAVE_COUNTS[member.squad] then counts:set(1, PLAZA_WAVE_COUNTS[member.squad]) end
return squad:place{counts=counts}
end)
end
end
group_start(context, state, group.id)
end

local function stage_plaza_barrage_start(context, state)
if plaza_stage(state) ~= PLAZA_WAVE_1 then return end
context:clear_variable(VAR_NEXT_STAGE)
context:clear_variable("hc13.plaza.strike")
context:set_variable(VAR_PLAZA_STAGE, PLAZA_BARRAGE)
stop_zavala_combat(context, state)
context:cancel_timer(TIMER_ZAVALA_SETTLE)
context:cancel_timer(TIMER_ZAVALA_SPAWN_RETRY)
play_cues(context, state, "barrage cue", PLAZA_CUES.barrage_start)
heroes.choose(context, state, "ward")
set_props(context, "rocket target off", {PROP_ROCKET_KILLS_ZAVALA}, false)
set_props(context, "damaging missiles off", BARRAGE_PROPS, false)
move_devices(context, "missiles off", {DEVICE_MISSILES}, MISSILES_OFF)
end

local function stage_plaza_barrage_end(context, state)
if state:variable("hc13.plaza.strike") == true then return end
context:set_variable("hc13.plaza.strike", true)
move_devices(context, "missiles", {DEVICE_MISSILES}, MISSILES_OFF)
set_props(context, "missiles off", BARRAGE_PROPS, false)
set_props(context, "bubble off", {PROP_BUBBLE_SHIELD}, false)
set_props(context, "targeted zavala rocket", {PROP_ROCKET_KILLS_ZAVALA}, true)
play_cues(context, state, "barrage end cue", PLAZA_CUES.barrage_end)
heroes.choose(context, state, "death")
end

local function stage_plaza_held(context, state)
context:clear_variable(VAR_NEXT_STAGE)
context:set_variable(VAR_PLAZA_STAGE, PLAZA_HELD)
context:set_variable("hc13.plaza.assaults_defended", 3)
stop_zavala_combat(context, state)
set_props(context, "orb spawner off", {PROP_ORB_SPAWNER}, false)
play_cues(context, state, "held cue", PLAZA_CUES.held)
play_cues(context, state, "leave cue", PLAZA_CUES.leave)
show_goal(context, DIRECTIVE_LEAVE_PLAZA, NAVPOINT_LEAVE_PLAZA)
arm(context, TRIGGER_PLAZA_EXIT) -- the campaign's zavala step ends on its report
end

local function plaza_try_first_wave(context, state)
if plaza_stage(state) ~= PLAZA_ARRIVED then return end
if state:variable(VAR_PLAZA_INIT_CLEAR) == true then
context:cancel_timer(TIMER_MEET_FALLBACK)
start_zavala_combat(context, state)
if state:variable("hc23.zavala.name") == "combat"
and state:variable("hc23.zavala.phase") == "staged" then
queue_plaza_stage(context, state, PLAZA_WAVE_1)
end
end
end

local function stage_plaza_meet(context, state)
if plaza_stage(state) == PLAZA_NONE then
start_plaza(context, state)
end
if state:variable(VAR_PLAZA_MET) == true then return end
context:set_variable(VAR_PLAZA_MET, true)
if plaza_stage(state) ~= PLAZA_ARRIVED then return end
set_props(context, "zavala unpinned", {PROP_ZAVALA_PINNED}, false)
set_props(context, "zavala grenade", {PROP_ZAVALA_GRENADE}, true)
if plaza_stage(state) ~= PLAZA_BARRAGE
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN
and (state:variable(VAR_ZAVALA_ALIVE) or 0) == 0 then
spawn_zavala_arrival(context, state)
end
if plaza_stage(state) ~= PLAZA_BARRAGE
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN then
start_zavala_combat(context, state)
end
show_goal(context, DIRECTIVE_DEFEND, nil)
context:start_timer(TIMER_MEET_FALLBACK, MEET_FALLBACK_MS)
plaza_try_first_wave(context, state)
end

local revival = {prefix="hc28.revive.", timer="hc28.revive.retry"}
function revival.active(state)
return state:variable(VAR_ZAVALA_DOWN) == true
and plaza_stage(state) == PLAZA_ZAVALA_DOWN
and state:variable(VAR_REGION) == PLAZA_REGION
end
function revival.arm(context, state)
if not revival.active(state) or state:variable(revival.prefix.."key")
or state:variable(revival.prefix.."owned") == true then return end
local tries = state:variable(revival.prefix.."tries") or 0
if tries >= 3 then
note_error(context, "zavala revive", "Interaction unavailable after three requests; inspect object state")
return
end
context:set_variable(revival.prefix.."tries", tries + 1)
local key = guarded(context, "zavala revive available", function()
return context:slot(PROP_DEAD_ZAVALA_INTERACT):set_interactable_object{
active=true, used=false, track_owner=true,
}
end)
if key and key.value then context:set_variable(revival.prefix.."key", key.value)
else
context:set_variable(revival.prefix.."unavailable", true)
context:start_timer(revival.timer, 1000)
end
end
function revival.observe(context, state, event)
if not revival.active(state) or not event.slot or event.slot.id ~= PROP_DEAD_ZAVALA_INTERACT then return end
local generation = event.generation
if type(generation) ~= "number" or generation <= 0
or generation < (state:variable(revival.prefix.."generation") or 0) then return end
context:set_variable(revival.prefix.."generation", generation)
local unavailable = event.present == false or event.interaction_open == false
context:set_variable(revival.prefix.."unavailable", unavailable)
context:set_variable(revival.prefix.."owned", event.has_owner == true)
if not unavailable then
context:cancel_timer(revival.timer)
context:clear_variable(revival.prefix.."tries")
elseif event.has_owner ~= true and not state:variable(revival.prefix.."key")
and state:timer(revival.timer) == nil then
context:start_timer(revival.timer, 1000)
end
end
function revival.effect(context, state, event)
if not event.request_key or event.request_key.value ~= state:variable(revival.prefix.."key") then return end
context:clear_variable(revival.prefix.."key")
if not revival.active(state) then return end
if event.outcome ~= OUTCOME_STAGED then
context:set_variable(revival.prefix.."unavailable", true)
note_error(context, "zavala revive request", event.outcome)
end
if state:variable(revival.prefix.."unavailable") == true
and state:variable(revival.prefix.."owned") ~= true then context:start_timer(revival.timer, 1000) end
end
function revival.timer_elapsed(context, state, event)
if event.timer_name == revival.timer and revival.active(state)
and state:variable(revival.prefix.."unavailable") == true
and state:variable(revival.prefix.."owned") ~= true then revival.arm(context, state) end
end
function revival.clear(context)
context:cancel_timer(revival.timer)
for _, field in ipairs({"key", "generation", "unavailable", "owned", "tries"}) do context:clear_variable(revival.prefix..field) end
end
local function stage_zavala_down(context, state)
audio.add(context, state, 47)
stop_zavala_combat(context, state)
context:set_variable(VAR_PLAZA_STAGE, PLAZA_ZAVALA_DOWN)
context:set_variable(VAR_ZAVALA_DOWN, true)
set_props(context, "dead zavala", {PROP_DEAD_ZAVALA_GHOST}, true)
move_devices(context, "dead zavala ghost", {DEVICE_DEAD_ZAVALA_GHOST}, GHOST_SHOW)
if state:variable(revival.prefix.."generation") == nil
or state:variable(revival.prefix.."unavailable") == true then revival.arm(context, state) end
end

local function stage_zavala_revived(context, state)
if state:variable(VAR_ZAVALA_DOWN) ~= true then return end
revival.clear(context)
context:set_variable("hc13.zavala.revived", true)
context:clear_variable(VAR_ZAVALA_DOWN)
guarded(context, "disable used zavala ghost", function()
return context:slot(PROP_DEAD_ZAVALA_INTERACT):set_interactable_object{
active = false, used = true,
}
end)
move_devices(context, "dead zavala ghost", {DEVICE_DEAD_ZAVALA_GHOST}, GHOST_HIDE)
set_props(context, "dead zavala off", {PROP_DEAD_ZAVALA_GHOST, PROP_DEAD_ZAVALA_INTERACT}, false)
context:set_variable(VAR_ZAVALA_ALIVE, 0)
heroes.choose(context, state, "revive")
end

local function plaza_restore(context, state)
if plaza_stage(state) == PLAZA_NONE then return end
disable_darkness(context)
if plaza_stage(state) < PLAZA_HELD then
hold_trigger(context, TRIGGER_PLAZA_EXIT)
else
arm(context, TRIGGER_PLAZA_EXIT)
end
if plaza_stage(state) == PLAZA_BARRAGE then
set_props(context, "damaging missiles restore off", BARRAGE_PROPS, false)
move_devices(context, "missiles restore off", {DEVICE_MISSILES}, MISSILES_OFF)
if state:variable("hc13.plaza.strike") ~= true then
set_props(context, "ward restore", {PROP_BUBBLE_SHIELD},
state:variable("hc23.zavala.name") == "ward"
and state:variable("hc23.zavala.phase") == "staged")
if state:variable("hc23.zavala.name") == "ward"
and state:variable("hc23.zavala.phase") == "staged"
and state:timer(TIMER_BARRAGE) == nil then
context:start_timer(TIMER_BARRAGE, BARRAGE_MS)
end
else
set_props(context, "ward restore off", {PROP_BUBBLE_SHIELD}, false)
if state:timer("hc13.zavala.death") == nil then
context:start_timer("hc13.zavala.death", 30000)
end
end
elseif plaza_stage(state) == PLAZA_ZAVALA_DOWN then
if state:variable(VAR_ZAVALA_DOWN) == true then
stage_zavala_down(context, state)
elseif state:variable("hc13.zavala.revived") == true then
set_props(context, "revived ghost restore off",
{PROP_DEAD_ZAVALA_GHOST, PROP_DEAD_ZAVALA_INTERACT}, false)
end
elseif plaza_stage(state) < PLAZA_HELD and state:variable(VAR_ZAVALA_DOWN) ~= true then
if plaza_stage(state) >= PLAZA_WAVE_1
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN then
start_zavala_combat(context, state)
end
end
if state:variable(VAR_NEXT_STAGE) ~= nil and state:timer(TIMER_NEXT_STAGE) == nil then
context:start_timer(TIMER_NEXT_STAGE, PLAZA_INTERMISSION_MS)
end
plaza_show_goal(context, state)
end

local GROUP_ON_CLEAR = {
hangar_start = function(context, state) gates_group_cleared(context, state, "hangar_start") end,
hangar_floor = function(context, state) gates_group_cleared(context, state, "hangar_floor") end,
bazaar = function(context, state) gates_group_cleared(context, state, "bazaar") end,
plaza_init = function(context, state)
context:cancel_timer(TIMER_PLAZA_SEED_RETRY)
context:set_variable(VAR_PLAZA_INIT_CLEAR, true)
plaza_try_first_wave(context, state)
end,
wave_1 = function(context, state)
queue_plaza_stage(context, state, PLAZA_BARRAGE)
end,
wave_2 = function(context, state)
queue_plaza_stage(context, state, PLAZA_WAVE_3)
end,
wave_3 = function(context, state)
queue_plaza_stage(context, state, PLAZA_HELD)
end,
}

local function group_settled(context, state, group)
context:clear_variable(group_var(group, "settling"))
if state:variable(group_var(group, "done")) == true then return end
local clear, seen, alive = group_status(context, state, group)
if not clear then return end
context:set_variable(group_var(group, "done"), true)
GROUP_ON_CLEAR[group.id](context, state)
end


local function on_region_changed(context, state, event)
if event.region_index == nil then return end
context:set_variable(VAR_REGION, event.region_index)
if event.region_index == BOULEVARD_REGION then heroes.choose(context,state,"ikora") end
gates_enter_region(context, state, event.region_index)
gates_confirm_pending(context, state)
if event.region_index == PLAZA_REGION then
context:clear_variable(VAR_PLAZA_HANDOFF)
start_plaza(context, state)
for _, trigger in ipairs(PLAZA_START_TRIGGERS) do arm(context, trigger) end
arm(context, TRIGGER_ZAVALA_MEET)
end
end

local function on_client_state_changed(context, state, event)
local region = event.current_region_index
if event.entered == true then
region = event.held_region_index or region or event.region_index
end
if region == nil then return end
context:set_variable(VAR_REGION, region)
if region == BOULEVARD_REGION and event.entered == true then heroes.choose(context,state,"ikora") end
if (region == BOULEVARD_REGION and event.entered == true)
or region == MILITARY_REGION then
gates_enter_region(context, state, region)
gates_confirm_pending(context, state)
end
if region == PLAZA_REGION then
context:clear_variable(VAR_PLAZA_HANDOFF)
start_plaza(context, state)
end
end

local function on_player_trigger(context, state, event)
local slot = event.slot
if slot == nil then return end
if slot.id ~= Slot.PT_GOTO_SKY_BATTLE then disarm(context, state, slot.id) end
if slot.id == TRIGGER_MILITARY_TO_PLAZA then
context:set_variable(VAR_PLAZA_HANDOFF, true)
end
if contains(PLAZA_START_TRIGGERS, slot.id) then
start_plaza(context, state)
elseif slot.id == Slot.PT_START_CABAL_MOVEMENT then
heroes.choose(context,state,"ikora")
elseif slot.id == Slot.PT_START_IKORA
and state:variable("hc13.ikora.played") ~= true then
context:set_variable("hc30.ikora.entry",true)
context:set_variable("hc13.ikora.played", true)
guarded(context, "ikora trigger disarm", function()
return context:slot(Slot.PT_START_IKORA):disarm_trigger()
end)
heroes.choose(context, state, "ikora")
elseif slot.id == TRIGGER_ZAVALA_MEET and plaza_stage(state) >= PLAZA_ARRIVED then
stage_plaza_meet(context, state)
elseif slot.id == TRIGGER_ZAVALA_MEET then
start_plaza(context, state)
stage_plaza_meet(context, state)
end
end

local function on_squad_state(context, state, event)
group_record(context, state, event)
local slot = event.slot
if slot == nil or slot.id ~= SLOT_ZAVALA or event.alive_count == nil then return end
context:set_variable(VAR_ZAVALA_ALIVE, event.alive_count)
if event.alive_count > 0 then
context:set_variable(VAR_ZAVALA_SEEN, true)
context:cancel_timer(TIMER_ZAVALA_SPAWN_RETRY)
context:cancel_timer(TIMER_ZAVALA_SETTLE)
if state:variable("hc23.zavala.name") == "revive"
and state:variable("hc23.zavala.phase") == "staged"
and state:variable("hc13.zavala.revived") == true then
start_zavala_combat(context, state)
end
if state:variable("hc23.zavala.combat_pending") == true then
start_zavala_combat(context, state)
end
if plaza_stage(state) ~= PLAZA_BARRAGE
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN
and plaza_stage(state) < PLAZA_HELD
and (state:variable(VAR_PLAZA_MET) == true
or plaza_stage(state) >= PLAZA_WAVE_1) then
start_zavala_combat(context, state)
end
elseif state:variable(VAR_ZAVALA_SEEN) == true
and state:variable(VAR_ZAVALA_DOWN) ~= true
and state:variable(VAR_PLAZA_MET) == true
and plaza_stage(state) >= PLAZA_ARRIVED and plaza_stage(state) < PLAZA_HELD
and plaza_stage(state) ~= PLAZA_BARRAGE
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN then
context:start_timer(TIMER_ZAVALA_SETTLE, ZAVALA_SETTLE_MS)
end
end

local function on_object_interacted(context, state, event)
local slot = event.slot
if slot ~= nil and slot.id == PROP_DEAD_ZAVALA_INTERACT
and state:variable(VAR_ZAVALA_DOWN) == true
and plaza_stage(state) == PLAZA_ZAVALA_DOWN
and state:variable(VAR_REGION) == PLAZA_REGION
and (event.generation == nil or event.generation >= (state:variable(revival.prefix.."generation") or 0)) then
stage_zavala_revived(context, state)
end
end

local function on_timer_elapsed(context, state, event)
local name = event.timer_name
if name == "hc13.ikora.watchdog" and state:variable("hc13.ikora.finished") ~= true then
note_error(context, "ikora boulevard", "Legacy scene attempt; restart mission to use v23 cast setup")
end
for _, gate in ipairs(GATES) do
if name == TIMER_RETRY_PREFIX .. gate.id and gate.reset_position
and state:variable(VAR_REGION) == gate.region
and gate_state(state, gate) == GATE_IDLE then
if not confirm_close(context, state, gate) then request_close(context, state, gate) end
elseif name == TIMER_RETRY_PREFIX .. gate.id
and state:variable(VAR_REGION) == gate.region
and (gate_state(state, gate) == GATE_REQUESTED
or gate_state(state, gate) == GATE_STAGED) then
local applied = guarded(context, gate.id .. " retry applied", function()
return context:slot(gate.device):applied{
channel = context.sdk.device_channels.position,
}
end)
if applied == true and state:variable(gate_var(gate, "key")) ~= nil then
context:set_variable(gate_var(gate, "state"), GATE_OPEN)
elseif gate.reset_position and state:variable(gate_var(gate, "key")) then
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
elseif (state:variable(gate_var(gate, "tries")) or 0) < MAX_OPEN_TRIES then
request_open(context, state, gate)
else
context:set_variable(gate_var(gate, "state"), GATE_FAILED)
note_error(context, gate.id, "open not applied")
end
end
end
for _, group in ipairs(GROUPS) do
if name == TIMER_GRACE_PREFIX .. group.id then
context:set_variable(group_var(group, "graced"), true)
group_evaluate(context, state, group)
elseif name == TIMER_SETTLE_PREFIX .. group.id then
group_settled(context, state, group)
end
end
if name == TIMER_MEET_FALLBACK then
context:set_variable("hc13.plaza.meet_elapsed", true)
plaza_try_first_wave(context, state)
elseif name == TIMER_NEXT_STAGE then
local next_stage = state:variable(VAR_NEXT_STAGE)
if next_stage == PLAZA_WAVE_1 then
stage_plaza_wave(context, state, 1)
elseif next_stage == PLAZA_BARRAGE then
stage_plaza_barrage_start(context, state)
elseif next_stage == PLAZA_WAVE_2 then
stage_plaza_wave(context, state, 2)
elseif next_stage == PLAZA_WAVE_3 then
stage_plaza_wave(context, state, 3)
elseif next_stage == PLAZA_HELD then
stage_plaza_held(context, state)
else
context:clear_variable(VAR_NEXT_STAGE)
end
elseif name == TIMER_PLAZA_SEED_RETRY then
local group = GROUP_BY_ID["plaza_init"]
local _, seen = group_status(context, state, group)
local tries = state:variable(VAR_PLAZA_SEED_TRIES) or 0
if seen == 0 and tries < MAX_PLAZA_SEED_TRIES then
seed_plaza_initial(context, state, true)
elseif seen == 0 then
note_error(context, "plaza_init", "no opening Cabal squad reports")
end
elseif name == TIMER_ZAVALA_SPAWN_RETRY then
note_error(context, "zavala", "Inspect hc23.zavala phase and receipts")
elseif name == TIMER_BARRAGE and plaza_stage(state) == PLAZA_BARRAGE then
stage_plaza_barrage_end(context, state)
elseif name == "hc13.zavala.death" and plaza_stage(state) == PLAZA_BARRAGE
and state:variable("hc13.plaza.strike") == true then
set_props(context, "targeted zavala rocket watchdog off", {PROP_ROCKET_KILLS_ZAVALA}, false)
note_error(context, "zavala death", "scene did not finish; inspect scene state/signals")
elseif name == TIMER_ZAVALA_SETTLE and state:variable(VAR_ZAVALA_ALIVE) == 0
and state:variable(VAR_ZAVALA_DOWN) ~= true
and plaza_stage(state) ~= PLAZA_BARRAGE
and plaza_stage(state) ~= PLAZA_ZAVALA_DOWN
and plaza_stage(state) < PLAZA_HELD then
note_error(context, "zavala", "Authored actor absent; preserve scene state for diagnosis")
end
end

local function on_effect_result(context, state, event)
local value = event.request_key.value
for _, gate in ipairs(GATES) do
local current = gate_state(state, gate)
if current == GATE_IDLE and gate.reset_position
and state:variable(gate_var(gate, "close_key")) == value
and state:variable(gate_var(gate, "closed")) ~= true then
if event.outcome ~= OUTCOME_STAGED then
context:clear_variable(gate_var(gate, "close_key"))
end
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
elseif (current == GATE_REQUESTED or current == GATE_STAGED)
and state:variable(gate_var(gate, "key")) == value then
if event.outcome == OUTCOME_STAGED then
if gate_state(state, gate) == GATE_REQUESTED then
context:set_variable(gate_var(gate, "state"), GATE_STAGED)
end
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
else
context:clear_variable(gate_var(gate, "key"))
local tries = state:variable(gate_var(gate, "tries")) or 0
if tries < MAX_OPEN_TRIES then
context:set_variable(gate_var(gate, "state"), GATE_REQUESTED)
context:start_timer(TIMER_RETRY_PREFIX .. gate.id, RETRY_DELAY_MS)
else
context:set_variable(gate_var(gate, "state"), GATE_FAILED)
note_error(context, gate.id, "open " .. tostring(event.outcome))
end
end
end
end
local label = REQUEST_LABELS[value]
if label ~= nil then
REQUEST_LABELS[value] = nil
if event.outcome ~= OUTCOME_STAGED then
note_error(context, label, event.outcome)
end
end
end

local function on_device_state(context, state, event)
local slot = event.slot
if slot == nil then return end
for _, gate in ipairs(GATES) do
local current = gate_state(state, gate)
if slot.id == gate.device and gate.reset_position
and (current == GATE_IDLE or current == GATE_FAILED)
and state:variable(gate_var(gate, "closed")) ~= true then
local key = event:applied_request{channel = context.sdk.device_channels.position}
if key ~= nil and key.value == state:variable(gate_var(gate, "close_key")) then
context:set_variable(gate_var(gate, "closed"), true)
context:set_variable(gate_var(gate, "state"), GATE_IDLE)
context:cancel_timer(TIMER_RETRY_PREFIX .. gate.id)
if group_done(state, gate.group) then
open_gate(context, state, gate, "cleared after close confirmed")
end
end
end
if slot.id == gate.device
and (current == GATE_REQUESTED or current == GATE_STAGED or current == GATE_FAILED) then
local key = event:applied_request{channel = context.sdk.device_channels.position}
if key ~= nil and key.value == state:variable(gate_var(gate, "key")) then
context:set_variable(gate_var(gate, "state"), GATE_OPEN)
context:cancel_timer(TIMER_RETRY_PREFIX .. gate.id)
end
end
end
end

local function on_fireteam_state(context, state, event)
local wiped = event.dead_count > 0 and event.alive_count == 0 and event.unknown_count == 0
if wiped and state:variable(VAR_WIPED) ~= true then
context:set_variable(VAR_WIPED, true)
context:set_variable(VAR_WIPES, (state:variable(VAR_WIPES) or 0) + 1)
if state:variable(VAR_REGION) == PLAZA_REGION then
disable_darkness(context)
end
elseif not wiped and state:variable(VAR_WIPED) == true then
context:clear_variable(VAR_WIPED)
end
end

local function on_load(context, state)
for _,gate in ipairs(GATES) do
if gate.reset_position and gate_state(state,gate)==GATE_FAILED
and state:variable(gate_var(gate,"closed"))==true and group_done(state,gate.group) then
context:set_variable(gate_var(gate,"state"),GATE_IDLE)
context:clear_variable(gate_var(gate,"key"));context:clear_variable(gate_var(gate,"tries"))
end
end
local region = state:variable(VAR_REGION)
if region ~= nil then
gates_enter_region(context, state, region)
if region == PLAZA_REGION then
if plaza_stage(state) == PLAZA_NONE then start_plaza(context, state)
else plaza_restore(context, state) end
end
end
gates_confirm_pending(context, state)
for _, group in ipairs(GROUPS) do
if state:variable(group_var(group, "settling")) == true
and state:timer(TIMER_SETTLE_PREFIX .. group.id) == nil then
context:clear_variable(group_var(group, "settling"))
end
group_evaluate(context, state, group)
end
end

heroes.on_staged = function(context, state, name)
if name == "ward" and plaza_stage(state) == PLAZA_BARRAGE
and state:variable("hc13.plaza.strike") ~= true then
set_props(context, "ward of dawn on", {PROP_BUBBLE_SHIELD}, true)
context:start_timer(TIMER_BARRAGE, BARRAGE_MS)
elseif name == "death" then
context:start_timer("hc13.zavala.death", 30000)
elseif name == "combat" then
context:set_variable(VAR_ZAVALA_COMBAT, true)
if plaza_stage(state) == PLAZA_ZAVALA_DOWN
and state:variable("hc13.zavala.revived") == true then
queue_plaza_stage(context, state, PLAZA_WAVE_2)
else plaza_try_first_wave(context, state) end
elseif (name == "arrival" or name == "revive")
and (state:variable(VAR_ZAVALA_ALIVE) or 0) > 0 then
if name == "revive" then
start_zavala_combat(context, state)
elseif state:variable("hc23.zavala.combat_pending") == true then
start_zavala_combat(context, state)
end
end
end

local function chain(name, extra)
local base = program[name]
program[name] = function(context, state, event)
if base ~= nil then base(context, state, event) end
extra(context, state, event)
end
end

chain("on_load", on_load)
chain("on_event_region_changed", on_region_changed)
chain("on_event_client_state_changed", on_client_state_changed)
chain("on_event_player_trigger", on_player_trigger)
chain("on_event_squad_state", on_squad_state)
chain("on_event_object_interacted", on_object_interacted)
chain("on_event_object_state", revival.observe)
chain("on_event_effect_result", revival.effect)
chain("on_event_timer_elapsed", revival.timer_elapsed)
chain("on_event_timer_elapsed", on_timer_elapsed)
chain("on_event_effect_result", on_effect_result)
chain("on_event_device_state", on_device_state)
chain("on_event_fireteam_state", on_fireteam_state)
chain("on_event_scene_finished", function(context, state, event)
if event.slot == nil then return end
if event.slot.id == Slot.SC_ZAVALA_SHIELD_DEATH
and plaza_stage(state) == PLAZA_BARRAGE
and state:variable("hc13.plaza.strike") == true
and state:variable("hc23.zavala.name") == "death"
and state:variable("hc23.zavala.phase") == "staged" then
context:set_variable("hc23.zavala.finished", true)
context:cancel_timer("hc13.zavala.death")
set_props(context, "targeted zavala rocket off", {PROP_ROCKET_KILLS_ZAVALA}, false)
guarded(context, "retire struck zavala", function()
return context:slot(Slot.SQ_ZAVALA_BANSHEE):retire_actor()
end)
stage_zavala_down(context, state)
elseif event.slot.id == Slot.SCENE_IKORA_BOULEVARD then
context:set_variable("hc13.ikora.finished", true)
context:cancel_timer("hc13.ikora.watchdog")
end
end)

end

install_homecoming_patch(program, mission, Slot, Squad, Directive)

local function install_opening_presentation(program)
local function chain(name, extra)
local base = program[name]
program[name] = function(context, state, event)
if base then base(context, state, event) end
extra(context, state, event)
end
end
local function request(context, label, run)
local ok, result = pcall(run)
if not ok then
context:set_variable("hc13.opening.error", string.sub(label .. ": " .. tostring(result), 1, 127))
return nil
end
if result and result.value then
context:set_variable("hc13.opening.req/" .. result.value, label)
end
return result
end
local function voice_once(context, state, number)
audio.add(context, state, number)
end

local function scene_once(context, state, name, cast)
local flag = "hc13.opening.scene/" .. name
if state:variable(flag) then return end
for _, squad in ipairs(cast or {}) do
local placed = request(context, name .. " cast", function()
return context:squad(squad):place{mode = context.sdk.squad_modes.replace}
end)
if not placed then return end
end
local key = request(context, name, function()
return context:scene(mission.Scene[name]):activate{spawn = false}
end)
if key then context:set_variable(flag, true) end
return key
end
local approach = {
{slot = Slot.SQUAD_FIRST_CONTACT_CABAL, label = "breach scene Cabal"},
{slot = Slot.SQUAD_FIRST_CONTACT_CABAL_BACKUP_A, label = "breach A"},
{slot = Slot.SQUAD_FIRST_CONTACT_CABAL_BACKUP_B, label = "breach B"},
{slot = Slot.SQ_CENTURION_INTRO, label = "stabbing Cabal"},
{slot = Slot.SQ_CENTURION_INTRO_RUSH, label = "corridor rush"},
{slot = Slot.SQ_CENTURION_INTRO_BACKUP, label = "drop-pod backup"},
}
local function approach_clear(state)
if state:variable("hc13.opening.cayde_near") ~= true then return false, "Waiting for Cayde entrance trigger" end
if state:variable("hc13.opening.stab") ~= true then return false, "Missing PT_CENTURION_INTRO_REINFORCE" end
if state:variable("hc13.opening.drop_pod") ~= true then return false, "Missing PT_DROP_POD" end
if state:variable("hc13.opening.corridor") ~= true then return false, "Missing PT_CIV_RUN_B" end
for i, member in ipairs(approach) do
if state:variable("hc13.opening.seen/" .. i) ~= true then
return false, "No live report: " .. member.label
elseif state:variable("hc13.opening.alive/" .. i) ~= 0 then
return false, "Approach still alive: " .. member.label
end
end
return true
end
local cayde_targets = {
{squad = Squad.SQ_LEGIONARYA, slot = Slot.SQ_LEGIONARYA, actor = Slot.SQ_LEGIONARYA_CELL_1},
{squad = Squad.SQ_LEGIONARYB, slot = Slot.SQ_LEGIONARYB, actor = Slot.SQ_LEGIONARYB_CELL_1},
{squad = Squad.SQ_LEGIONARYC, slot = Slot.SQ_LEGIONARYC, actor = Slot.SQ_LEGIONARYC_CELL_1},
}
local cayde_doors = {Slot.O_SHAXX_DOOR_ENTER, Slot.O_SHAXX_DOOR_EXIT, Slot.O_SHAXX_BLOCKER}
local cayde_sources = {Squad.SQ_CAYDE, Squad.SQ_LEGIONARYA, Squad.SQ_LEGIONARYB,
Squad.SQ_LEGIONARYC, Squad.SQUAD_INVISIBLE_SHOOT_TARGET_1}
local cayde_cast = {Slot.SQ_CAYDE, Slot.SQ_LEGIONARYA, Slot.SQ_LEGIONARYB,
Slot.O_SHAXX_DOOR_EXIT, Slot.O_SHAXX_DOOR_ENTER, Slot.SQ_LEGIONARYC,
Slot.SQUAD_INVISIBLE_SHOOT_TARGET_1}
local function cayde_error(context, message)
context:set_variable("hc14.cayde.error", string.sub(message, 1, 127))
context:set_variable("debug.cayde.wait", string.sub(message, 1, 127))
end
local function release_cayde_blocker(context, state)
local phase = state:variable("hc14.cayde.phase")
if state:variable("hc21.cayde.setup") ~= true
or state:variable("hc21.cayde.release_staged") ~= true
or (phase ~= "scene_staged" and phase ~= "finished") then return end
if state:variable("hc14.cayde.door_present/3") == false then
context:set_variable("hc22.cayde.blocker_released", true)
context:clear_variable("hc22.cayde.blocker_request")
context:clear_variable("hc22.cayde.blocker_requested")
context:cancel_timer("hc14.cayde.scene_watchdog")
context:clear_variable("hc14.cayde.error")
context:set_variable("debug.cayde.wait", "Cayde collision blocker absent; authored doors retain control")
return
end
if state:variable("hc22.cayde.blocker_released") == true
or state:variable("hc22.cayde.blocker_requested") == true
or state:variable("hc22.cayde.blocker_request") ~= nil then return end
context:clear_variable("hc14.cayde.error")
context:clear_variable("hc22.cayde.blocker_staged")
local key = request(context, "Cayde collision handoff", function()
return context:slot(Slot.O_SHAXX_BLOCKER):set_object_active{active = false}
end)
if key and key.value then
context:set_variable("hc22.cayde.blocker_request", key.value)
context:set_variable("hc22.cayde.blocker_requested", true)
context:set_variable("debug.cayde.wait", "Cayde collision handoff queued; awaiting blocker absence")
else
cayde_error(context, state:variable("hc13.opening.error") or "Cayde blocker handoff returned no request key")
end
end
local function activate_cayde(context, state)
if state:variable("hc21.cayde.setup") ~= true
or state:variable("hc14.cayde.phase") ~= "binding_targets" then return end
local clear, reason = approach_clear(state)
context:set_variable("debug.cayde.approach_clear", clear == true)
if not clear then context:set_variable("debug.cayde.wait", reason); return end
for i = 1, #cayde_sources do
if state:variable("hc21.cayde.reserved/" .. i) ~= true then return end
end
for i = 1, #cayde_targets do
if state:variable("hc21.cayde.bound/" .. i) ~= true then return end
end
context:clear_variable("hc14.cayde.error")
context:set_variable("debug.cayde.wait", "Requesting Cayde scene with seven authored participants")
context:set_variable("hc14.cayde.phase", "scene_requested")
local key = request(context, "Cayde scene with reserved cast", function()
return context:scene(mission.Scene.SCENE_CAYDE_GOLDEN_GUN):activate{
spawn = false, cast = cayde_cast,
}
end)
if key and key.value then
context:set_variable("hc21.cayde.scene_request", key.value)
context:cancel_timer("hc14.cayde.ready_watchdog")
context:start_timer("hc14.cayde.scene_watchdog", 60000)
else
context:set_variable("hc14.cayde.phase", "failed")
cayde_error(context, state:variable("hc13.opening.error") or "Cayde scene returned no request key")
end
end
local function bind_cayde_cast(context, state)
if state:variable("hc14.cayde.phase") ~= "reserving_cast" then return end
for i = 1, #cayde_sources do
if state:variable("hc21.cayde.reserved/" .. i) ~= true then return end
end
context:set_variable("hc14.cayde.phase", "binding_targets")
context:set_variable("debug.cayde.wait", "Binding the three reserved Legionary actors")
for i, member in ipairs(cayde_targets) do
local key = request(context, "Cayde target binding " .. i, function()
return context:slot(member.actor):bind_combatant_to_squad()
end)
if not key or not key.value then
context:set_variable("hc14.cayde.phase", "failed")
cayde_error(context, state:variable("hc13.opening.error") or "Target binding returned no request key")
return
end
context:set_variable("hc21.cayde.bind_request/" .. i, key.value)
end
end
local function prepare_cayde(context, state)
if state:variable("hc14.cayde.phase") ~= nil or not approach_clear(state) then return end
context:set_variable("hc21.cayde.setup", true)
context:set_variable("hc14.cayde.phase", "reserving_cast")
context:set_variable("debug.cayde.wait", "Reserving Cayde, three Legionaries, and the authored aiming target")
for i, squad in ipairs(cayde_sources) do
local key = request(context, "Cayde source reservation " .. i, function()
return context:squad(squad):place{mode = context.sdk.squad_modes.reserve}
end)
if not key or not key.value then
context:set_variable("hc14.cayde.phase", "failed")
cayde_error(context, state:variable("hc13.opening.error") or "Source reservation returned no request key")
return
end
context:set_variable("hc21.cayde.reserve_request/" .. i, key.value)
end
context:start_timer("hc14.cayde.ready_watchdog", 15000)
end
local function check_cayde(context, state)
if state:variable("hc14.cayde.phase") == "binding_targets" then
activate_cayde(context, state)
return
elseif state:variable("hc14.cayde.phase") ~= nil then
context:set_variable("debug.cayde.wait", state:variable("hc14.cayde.error")
or "Cayde phase: " .. state:variable("hc14.cayde.phase"))
return
end
local clear, reason = approach_clear(state)
context:set_variable("debug.cayde.approach_clear", clear == true)
if clear then
context:set_variable("debug.cayde.wait", "Approach clear; settling")
if not state:timer("hc13.opening.cayde_clear") then
context:start_timer("hc13.opening.cayde_clear", 1500)
end
else
context:set_variable("debug.cayde.wait", reason)
context:cancel_timer("hc13.opening.cayde_clear")
end
end
local function reset_wall(context, state)
if state:variable("hc13.opening.wall_initialized") then return end
local key = request(context, "wall intact position 0", function()
return context:slot(Slot.D_UNDERWATCH_COLLAPSING_WALL):set_channel{
channel = context.sdk.device_channels.position,
value = context.sdk.unit(0), snap = true,
}
end)
if key then context:set_variable("hc13.opening.wall_initialized", true) end
end
local previous_staged = heroes.on_staged
heroes.on_staged = function(context, state, name)
if previous_staged then previous_staged(context,state,name) end
if name == "breach" then
if state:variable("hc13.opening.wall") ~= true then
reset_wall(context, state)
local key = request(context, "wall animated break", function()
return context:slot(Slot.D_UNDERWATCH_COLLAPSING_WALL):set_channel{
channel = context.sdk.device_channels.position,
value = context.sdk.unit(1), snap = false,
}
end)
if key then context:set_variable("hc13.opening.wall", true) end
voice_once(context, state, cue.CUE_4)
context:start_timer("hc13.opening.cabal_line", 1500)
end
end
end
local function cayde_door(context, state, force)
local phase = state:variable("hc14.cayde.phase")
if phase ~= nil then return end
if state:variable("hc14.cayde.close_requested") and not force then return end
for i, slot in ipairs(cayde_doors) do
local key = request(context, "Cayde close object " .. i, function()
return context:slot(slot):set_object_active{active = true}
end)
if not key then
cayde_error(context, "Cayde door activation failed: " .. i)
return
end
end
context:set_variable("hc14.cayde.close_requested", true)
end
chain("on_event_region_changed", function(context, state, event)
if event.region_index == mission.states.STATE_80B500BC_0009_0000_80B500BB.region_index then
reset_wall(context, state)
cayde_door(context, state, false)
end
end)
chain("on_event_client_state_changed", function(context, state, event)
if event.entered == true
and (event.current_region_index or event.region_index) == mission.states.STATE_80B500BC_0009_0000_80B500BB.region_index then
cayde_door(context, state, true)
end
end)
chain("on_event_object_state", function(context, state, event)
if not event.slot then return end
for i, slot in ipairs(cayde_doors) do
if event.slot.id == slot and event.present ~= nil then
context:set_variable("hc14.cayde.door_present/" .. i, event.present)
if slot == Slot.O_SHAXX_BLOCKER and event.present == false then
release_cayde_blocker(context, state)
end
activate_cayde(context, state)
return
end
end
end)
chain("on_event_squad_state", function(context, state, event)
if not event.slot or event.alive_count == nil then return end
if event.slot.id == Slot.SQ_CAYDE then
context:set_variable("hc19.cayde.alive", event.alive_count)
activate_cayde(context, state)
return
end
for i, member in ipairs(cayde_targets) do
if event.slot.id == member.slot then
context:set_variable("hc14.cayde.target_alive/" .. i, event.alive_count)
activate_cayde(context, state)
return
end
end
for i, member in ipairs(approach) do
if event.slot.id == member.slot then
context:set_variable("hc13.opening.alive/" .. i, event.alive_count)
if event.alive_count > 0 then context:set_variable("hc13.opening.seen/" .. i, true) end
check_cayde(context, state)
return
end
end
end)
chain("on_load", function(context, state)
context:set_variable("debug.cayde.version", 22)
if state:variable("hc14.cayde.phase") ~= nil
and state:variable("hc21.cayde.setup") ~= true then
cayde_error(context, "Old Cayde attempt: return to orbit and start a fresh mission with v22")
return
end
check_cayde(context, state)
release_cayde_blocker(context, state)
end)
chain("on_event_player_trigger", function(context, state, event)
context:set_variable("debug.cayde.version", 22)
if not event.slot then return end
local id = event.slot.id
if id == Slot.PT_START then
reset_wall(context, state)
cayde_door(context, state, false)
voice_once(context, state, cue.CUE_1)
elseif id == Slot.PT_WALL_EXPLODE then
heroes.choose(context, state, "breach")
elseif id == Slot.PT_CENTURION_INTRO then
heroes.choose(context, state, "centurion")
elseif id == Slot.PT_CENTURION_INTRO_REINFORCE then
context:set_variable("hc13.opening.stab", true)
check_cayde(context, state)
elseif id == Slot.PT_DROP_POD then
context:set_variable("hc13.opening.drop_pod", true)
check_cayde(context, state)
elseif id == Slot.PT_CIV_RUN_B then
context:set_variable("hc13.opening.corridor", true)
check_cayde(context, state)
elseif id == Slot.PT_SHAXX_ENTERS or id == Slot.PT_SHAXX_ENTERS_BACKUP then
cayde_door(context, state, false)
context:set_variable("hc13.opening.cayde_near", true)
check_cayde(context, state)
elseif id == Slot.PT_START_SHAXX_SCENE or id == Slot.PT_PLAYER_NEAR_SHAXX then
if state:variable("hc13.opening.scene/SCENE_SHAXX") ~= true then
heroes.choose(context, state, "shaxx")
elseif not state:variable("hc23.shaxx.name") then
context:set_variable("hc23.shaxx.error", "Legacy scene attempt; restart mission to use v23 cast setup")
end
elseif id == Slot.PT_POSTGUN then
voice_once(context, state, cue.CUE_30)
elseif id == Slot.PT_DIALOGUE_HANGAR_WINDOW then
voice_once(context, state, cue.CUE_34)
end
end)
chain("on_event_timer_elapsed", function(context, state, event)
if event.timer_name == "hc13.opening.cayde_clear" then
prepare_cayde(context, state)
elseif event.timer_name == "hc14.cayde.ready_watchdog" then
local phase = state:variable("hc14.cayde.phase")
if phase == "reserving_cast" or phase == "binding_targets" then
cayde_error(context, "Cayde preparation pending: " .. phase .. "; inspect hc21.cayde receipt variables")
end
elseif event.timer_name == "hc14.cayde.scene_watchdog" then
if state:variable("hc21.cayde.release_staged") == true then
if state:variable("hc22.cayde.blocker_released") ~= true then
cayde_error(context, "Cayde collision handoff pending; inspect blocker receipt and presence")
end
elseif state:variable("hc14.cayde.phase") ~= "finished" then
cayde_error(context, "Cayde entry handoff pending; inspect scene and entry-event receipts")
end
elseif event.timer_name == "hc13.opening.cabal_line" then
voice_once(context, state, cue.CUE_5)
if state:variable("hc13.opening.cue/5") == true
and state:variable("hc13.opening.cue/6") ~= true
and state:timer("hc13.opening.cabal_explanation") == nil then
context:start_timer("hc13.opening.cabal_explanation", 2500)
end
elseif event.timer_name == "hc13.opening.cabal_explanation"
and state:variable("hc13.opening.cue/5") == true then
voice_once(context, state, cue.CUE_6)
end
end)
chain("on_event_effect_result", function(context, state, event)
for i = 1, #cayde_sources do
if event.request_key.value == state:variable("hc21.cayde.reserve_request/" .. i) then
context:clear_variable("hc21.cayde.reserve_request/" .. i)
if event.outcome == "transport_staged" then
context:set_variable("hc21.cayde.reserved/" .. i, true)
bind_cayde_cast(context, state)
else
context:set_variable("hc14.cayde.phase", "failed")
context:cancel_timer("hc14.cayde.ready_watchdog")
cayde_error(context, "Source reservation " .. i .. " failed: " .. event.outcome)
end
end
end
for i = 1, #cayde_targets do
if event.request_key.value == state:variable("hc21.cayde.bind_request/" .. i) then
context:clear_variable("hc21.cayde.bind_request/" .. i)
if event.outcome == "transport_staged" then
context:set_variable("hc21.cayde.bound/" .. i, true)
activate_cayde(context, state)
else
context:set_variable("hc14.cayde.phase", "failed")
context:cancel_timer("hc14.cayde.ready_watchdog")
cayde_error(context, "Target binding " .. i .. " failed: " .. event.outcome)
end
end
end
local flag = "hc13.opening.req/" .. event.request_key.value
local label = state:variable(flag)
if label then
context:clear_variable(flag)
if event.outcome ~= "transport_staged" then
context:set_variable("hc13.opening.error", string.sub(label .. ": " .. event.outcome, 1, 127))
end
end
if event.request_key.value == state:variable("hc21.cayde.scene_request") then
context:clear_variable("hc21.cayde.scene_request")
if event.outcome == "transport_staged" then
if state:variable("hc21.cayde.setup") == true
and state:variable("hc14.cayde.phase") == "scene_requested"
and state:variable("hc21.cayde.release_requested") ~= true then
local key = request(context, "Cayde authored entry event", function()
return context:scene(mission.Scene.SCENE_CAYDE_GOLDEN_GUN):send_event{key = 0x6F51AC66}
end)
if key and key.value then
context:set_variable("hc21.cayde.release_requested", true)
context:set_variable("hc21.cayde.release_request", key.value)
context:set_variable("hc14.cayde.phase", "scene_staged")
context:set_variable("debug.cayde.wait", "Cayde entry event queued; awaiting native completion")
else
context:set_variable("hc14.cayde.phase", "failed")
context:cancel_timer("hc14.cayde.scene_watchdog")
cayde_error(context, state:variable("hc13.opening.error") or "Cayde entry event returned no request key")
end
end
else
context:set_variable("hc14.cayde.phase", "failed")
context:cancel_timer("hc14.cayde.scene_watchdog")
cayde_error(context, "Cayde scene refused: " .. event.outcome)
end
end
if event.request_key.value == state:variable("hc21.cayde.release_request") then
context:clear_variable("hc21.cayde.release_request")
if event.outcome == "transport_staged" then
context:set_variable("hc21.cayde.release_staged", true)
if state:variable("hc14.cayde.phase") == "scene_staged" then
context:set_variable("debug.cayde.wait", "Cayde entry event staged; awaiting native completion")
end
release_cayde_blocker(context, state)
else
context:set_variable("hc14.cayde.phase", "failed")
context:cancel_timer("hc14.cayde.scene_watchdog")
cayde_error(context, "Cayde entry event refused: " .. event.outcome)
end
end
if event.request_key.value == state:variable("hc22.cayde.blocker_request") then
if event.outcome == "transport_staged" then
context:set_variable("hc22.cayde.blocker_staged", true)
context:set_variable("debug.cayde.wait", "Cayde collision handoff staged; awaiting blocker absence")
else
context:clear_variable("hc22.cayde.blocker_request")
context:clear_variable("hc22.cayde.blocker_requested")
context:clear_variable("hc22.cayde.blocker_staged")
cayde_error(context, "Cayde blocker handoff refused: " .. event.outcome)
end
end
end)
chain("on_event_scene_finished", function(context, state, event)
heroes.finish(context,state,event)
if not event.slot then return end
if event.slot.id == Slot.SCENE_CAYDE_GOLDEN_GUN
and state:variable("hc21.cayde.setup") == true
and (state:variable("hc14.cayde.phase") == "scene_requested"
or state:variable("hc14.cayde.phase") == "scene_staged") then
context:set_variable("hc14.cayde.phase", "finished")
context:set_variable("debug.cayde.wait", "Cayde scene completed")
context:set_variable("hc14.cayde.activation_token", tostring(event.activation_token))
release_cayde_blocker(context, state)
end
end)
end
install_opening_presentation(program)


local function install_military_presentation(program)
local p="hc36.military."
local region=mission.states.STATE_80B500BC_0004_0000_80B500B3.region_index
local corridor={"SQ_HANGAR_OVERLOOK_B_B","SQ_HANGAR_OVERLOOK_B_C","SQ_HANGAR_OVERLOOK_SNIPER"}
local objects={Slot.CABAL_DESTROYER,Slot.O_CABAL_MISSILE_1_80B5036A,
Slot.O_CABAL_MISSILE_2_80B5036A,Slot.O_CABAL_MISSILE_3_80B5036A,
Slot.O_CABAL_MISSILE_4,Slot.O_CABAL_MISSILE_5,Slot.O_CABAL_MISSILE_6}
local pump
pump=function(c,s)
if s:variable("hc13.region")~=region or s:variable(p.."key") or s:timer(p.."retry") then return end
local step=s:variable(p.."step") or 1
if step>6+#objects then return end
if s:variable("hc38.military.relocated")==nil then
local fresh=step==1 and not s:variable(p.."step")
for i=1,3 do if s:variable("hc26.g.hangar_floor.s"..i)==true then fresh=false end end
c:set_variable("hc38.military.relocated",fresh)
end
if (step==3 or step==4) and s:variable("hc38.military.relocated")==true then
c:set_variable(p.."step",5);pump(c,s);return
end
if s:variable("hc13.amanda_start.state")~=3 then return end
if step>6 and s:variable("hc13.amanda_hangar.state")~=3 then return end
-- The source presence report is separate from the activation transport receipt.
if step>7 and s:variable(p.."ship_present")~=true then return end
if step<=6 then
local member=(step+1)//2
-- A reload of a v34 attempt must not replace already observed corridor squads.
if s:variable("hc26.g.hangar_floor.s"..member)==true then
c:set_variable(p.."step",member*2+1);pump(c,s);return
end
end
local tries=(s:variable(p.."tries") or 0)+1
if tries>3 then return end
c:set_variable(p.."tries",tries)
local ok,key=pcall(function()
if step<=6 then
local symbol=corridor[(step+1)//2]
if step%2==1 then return c:slot(Slot[symbol]):assign_combat_objective{objective=c:slot(Slot.OBJ_HANGAR)} end
local squad=c:squad(Squad[symbol])
if step==2 and s:variable("hc38.military.relocated")==true then
local counts=squad:counts()
counts:set(1,1);counts:set(2,3);counts:set(3,2)
return squad:place{counts=counts}
end
return squad:place{}
end
return c:slot(objects[step-6]):set_object_active{active=true}
end)
if not ok or not key or not key.value then
c:set_variable(p.."error",string.sub(tostring(key),1,127))
if tries<3 then c:start_timer(p.."retry",1000) end
return
end
c:set_variable(p.."step",step);c:set_variable(p.."key",key.value)
end
local function chain(name,fn)
local old=program[name]
program[name]=function(c,s,e)
if e and e.attempt_generation and e.attempt_generation~=c.attempt_generation then return end
if old then old(c,s,e) end;fn(c,s,e or {})
end
end
chain("on_event_effect_result",function(c,s,e)
if e.request_key and e.request_key.value==s:variable(p.."key") then
c:clear_variable(p.."key")
if e.outcome=="transport_staged" then
c:clear_variable(p.."tries");c:clear_variable(p.."error")
c:set_variable(p.."step",(s:variable(p.."step") or 1)+1)
else
c:set_variable(p.."error","Request "..tostring(e.outcome))
if (s:variable(p.."tries") or 0)<3 then c:start_timer(p.."retry",1000) end
end
end
pump(c,s)
end)
chain("on_event_object_state",function(c,s,e)
if e.slot and e.slot.id==objects[1] and e.present~=nil then
c:set_variable(p.."ship_present",e.present==true)
end
pump(c,s)
end)
chain("on_event_device_state",pump)
chain("on_event_squad_state",pump)
chain("on_event_timer_elapsed",pump)
chain("on_event_region_changed",pump)
chain("on_event_client_state_changed",pump)
chain("on_load",function(c,s)
if not s:variable(p.."step") and s:variable("hc28.military.step") then
-- Old presentations may have already flown away; do not replay them on reload.
if s:variable("hc28.military.step")>1 then
c:set_variable(p.."step",14)
c:set_variable(p.."error","Legacy ship presentation retained; fresh mission uses new door timing")
end
end
pump(c,s)
end)
end
install_military_presentation(program)
do
local p="hc30.pod."
local pod=Slot.O_CABAL_DROP_POD_MILITARY_HALLWAY
local region=mission.states.STATE_80B500BC_0004_0000_80B500B3.region_index
local pump
pump=function(c,s)
if s:variable("hc13.region")~=region then return end
local n=s:variable(p.."phase")
if not n or n>=4 or s:variable(p.."key") then return end
if s:variable(p.."staged") then
if n<=2 and s:variable(p.."present")~=true then return end
n=n+1;c:set_variable(p.."phase",n)
c:clear_variable(p.."staged");c:clear_variable(p.."tries");c:clear_variable(p.."error")
c:cancel_timer(p.."watch")
if n>=4 then return end
end
local tries=(s:variable(p.."tries")or 0)+1
if tries>3 then return end
c:set_variable(p.."tries",tries)
local ok,key=pcall(function()
if n==1 then return c:slot(pod):set_object_active{active=true}
elseif n==2 then return c:slot(Slot.SQ_MILITARY_HALLWAY_DESTRUCTION):assign_combat_objective{objective=c:slot(Slot.OBJ_HANGAR)}
else return c:squad(Squad.SQ_MILITARY_HALLWAY_DESTRUCTION):place{spawn_rule=c:slot(Slot.SR_MILITARY_HALLWAY_DESTRUCTION)} end
end)
if ok and key and key.value then
c:set_variable(p.."key",key.value);c:start_timer(p.."watch",5000)
else
c:set_variable(p.."error",string.sub(tostring(key),1,127))
if tries<3 then c:start_timer(p.."retry",1000) end
end
end
local function chain(name,fn)
local old=program[name]
program[name]=function(c,s,e)
if e and e.attempt_generation and e.attempt_generation~=c.attempt_generation then return end
if old then old(c,s,e) end;fn(c,s,e)
end
end
chain("on_event_player_trigger",function(c,s,e)
if e.slot and e.slot.id==Slot.PT_HANGAR_SPAWN then
if not s:variable(p.."phase") then c:set_variable(p.."phase",1) end
pump(c,s)
end
end)
chain("on_event_object_state",function(c,s,e)
if e.slot and e.slot.id==pod then c:set_variable(p.."present",e.present==true);pump(c,s) end
end)
chain("on_event_effect_result",function(c,s,e)
if not e.request_key or e.request_key.value~=s:variable(p.."key") then return end
c:clear_variable(p.."key")
if e.outcome=="transport_staged" then c:set_variable(p.."staged",true);pump(c,s)
else
c:cancel_timer(p.."watch");c:set_variable(p.."error","Request "..tostring(e.outcome))
if (s:variable(p.."tries")or 0)<3 then c:start_timer(p.."retry",1000) end
end
end)
chain("on_event_timer_elapsed",function(c,s,e)
if e.timer_name==p.."retry" then pump(c,s)
elseif e.timer_name==p.."watch" and (s:variable(p.."phase")or 4)<4 then
c:set_variable(p.."error","Awaiting pod presence or exact request outcome")
end
end)
chain("on_load",pump)
chain("on_event_region_changed",pump)
chain("on_event_client_state_changed",pump)
end

local function install_scene_diagnostics(program)
local subjects = {
{name = "cayde", config = mission.scenes.SCENE_CAYDE_GOLDEN_GUN.config_tag,
slot = Slot.SCENE_CAYDE_GOLDEN_GUN},
{name = "centurion", config = mission.scenes.SC_CENTURION_INTRO.config_tag,
slot = Slot.SC_CENTURION_INTRO},
}
local timer = "debug.scene.scan"
local function chain(name, extra)
local base = program[name]
program[name] = function(context, state, event)
if base then base(context, state, event) end
extra(context, state, event)
end
end
local function scan(context, state)
local ok, message = pcall(function()
local catalog = context.sdk.catalog
if not catalog then
context:set_variable("debug.scene.status", "Live catalog not ready; re-enter an entrance trigger or reload")
context:clear_variable("debug.scene.scan_row")
return
end
local edges = catalog.authored_scene_squad_edges
context:set_variable("debug.scene.total_edges", edges.count)
local first = state:variable("debug.scene.scan_row") or 1
local last = math.min(first + 127, edges.count)
for row = first, last do
local edge = edges:at(row)
if row <= 2 then
context:set_variable("debug.scene.sample" .. row, string.sub(
"scene_idx=" .. tostring(edge.scene_slot_index)
.. " squad_idx=" .. tostring(edge.squad_slot_index)
.. " config=" .. tostring(edge.config_tag), 1, 127))
end
for _, subject in ipairs(subjects) do
local prefix = "debug.scene." .. subject.name
if edge.config_tag == subject.config then
context:set_variable(prefix .. ".config_matches", (state:variable(prefix .. ".config_matches") or 0) + 1)
end
local matched_base = nil
for base = 0, 1 do
local resolved, matches = pcall(function()
return catalog.slots:at(edge.scene_slot_index + base).id == subject.slot
end)
if resolved and matches then matched_base = base; break end
end
if matched_base ~= nil then
local count = (state:variable(prefix .. ".links") or 0) + 1
context:set_variable(prefix .. ".links", count)
if count <= 4 then
local info = "scene_idx=" .. tostring(edge.scene_slot_index)
.. " squad_idx=" .. tostring(edge.squad_slot_index)
.. " target=" .. tostring(edge.target_object_key)
context:set_variable(prefix .. ".link" .. count, string.sub(info, 1, 127))
local resolved, name = pcall(function()
local squad_slot = catalog.slots:at(edge.squad_slot_index + matched_base)
return squad_slot.name or squad_slot.id
end)
if resolved and name then
context:set_variable(prefix .. ".squad" .. count, string.sub(name, 1, 127))
end
end
end
end
end
context:set_variable("debug.scene.scan_row", last + 1)
if last < edges.count then
context:set_variable("debug.scene.status", "Scanning " .. last .. "/" .. edges.count)
context:start_timer(timer, 1)
else
context:set_variable("debug.scene.status", "Complete: edges=" .. edges.count
.. " cayde=" .. tostring(state:variable("debug.scene.cayde.links"))
.. " centurion=" .. tostring(state:variable("debug.scene.centurion.links")))
end
end)
if not ok then
context:set_variable("debug.scene.status", string.sub("Catalog read failed: " .. tostring(message), 1, 127))
context:clear_variable("debug.scene.scan_row")
end
end
local function start(context, state)
context:cancel_timer(timer)
context:set_variable("debug.scene.build", 18)
context:set_variable("debug.scene.scan_row", 1)
context:clear_variable("debug.scene.total_edges")
context:clear_variable("debug.scene.sample1")
context:clear_variable("debug.scene.sample2")
for _, subject in ipairs(subjects) do
local prefix = "debug.scene." .. subject.name
context:set_variable(prefix .. ".links", 0)
context:set_variable(prefix .. ".config_matches", 0)
for i = 1, 4 do
context:clear_variable(prefix .. ".link" .. i)
context:clear_variable(prefix .. ".squad" .. i)
end
end
scan(context, state)
end
chain("on_load", start)
chain("on_event_player_trigger", function(context, state, event)
if event.slot and (event.slot.id == Slot.PT_SHAXX_ENTERS
or event.slot.id == Slot.PT_SHAXX_ENTERS_BACKUP
or event.slot.id == Slot.PT_CENTURION_INTRO_REINFORCE)
and state:variable("debug.scene.scan_row") == nil then start(context, state) end
end)
chain("on_event_timer_elapsed", function(context, state, event)
if event.timer_name == timer then scan(context, state) end
end)
end
install_scene_diagnostics(program)

do
local function chain(name, extra)
local base = program[name]
program[name] = function(context, state, event)
if base then base(context, state, event) end
extra(context, state, event)
end
end
chain("on_load", heroes.load)
chain("on_event_effect_result", heroes.effect)
chain("on_event_timer_elapsed", heroes.timer)
end
local function install_amanda_pickup(program)
local p = "hc23.amanda."
local cinema = Slot.MID_CINEMATIC_CINEMATIC
local hawk, pickup = Slot.O_HAWK_1, Slot.PT_GOTO_SKY_BATTLE
local dialogue = Slot.M_DIALOG_SENSOR_80B50913
-- Authored cue76 window from bank80C2AF61; elapsed time is not playback completion.
local window_ms = 2500
local cine_state = mission.states.STATE_80B500BC_0008_0001_80B500B9
local ship_state = mission.states.STATE_80B500BC_0008_0000_80B500B8
local boulevard = mission.states.STATE_80B500BC_0000_0000_80B500AD
local sources = {Slot.SQ_BAZAAR_START, Slot.SQ_BAZAAR_TEASE_A, Slot.SQ_BAZAAR_TEASE_B,
Slot.SQ_BAZAAR_A_A, Slot.SQ_BAZAAR_A_B, Slot.SQ_BAZAAR_A_C,
Slot.SQ_BAZAAR_FINALE, Slot.SQ_FLAME}
local function chain(name, extra)
local base = program[name]
program[name] = function(context, state, event)
if event and event.attempt_generation and event.attempt_generation~=context.attempt_generation then return end
if base then base(context, state, event) end
extra(context, state, event)
end
end
local function fail(context, message)
context:set_variable(p.."phase", "failed")
context:set_variable(p.."error", string.sub(tostring(message), 1, 127))
context:cancel_timer(p.."watchdog")
end
local function request(context, phase, run)
context:set_variable(p.."policy",37)
context:set_variable(p.."phase", phase)
local ok, key = pcall(run)
if not ok or not key or not key.value then
fail(context, ok and "No request key returned" or key)
return
end
context:set_variable(p.."key", key.value)
context:start_timer(p.."watchdog", 60000)
end
local function clear(state)
if state:variable(p.."entered") ~= true then return false end
for i = 1, #sources do
if state:variable(p.."seen/" .. i) ~= true
or state:variable(p.."alive/" .. i) ~= 0 then return false end
end
return true
end
local function board(context, state)
if state:variable(p.."phase") == "boarding"
and state:variable(p.."boarding_trigger") == true
and state:variable(p.."ship_present") == true and state:variable(p.."window_elapsed")==true and clear(state)
and state:variable(p.."region") == boulevard.region_index then
request(context, "disarming", function() return context:slot(pickup):disarm_trigger() end)
end
end
local function ship_ready(context, state)
if state:variable(p.."phase") == "ship_waiting" and clear(state)
and state:variable(p.."region") == boulevard.region_index
and state:variable(p.."ship_present") == true then
request(context, "dialogue_dispatch", function()
return context:slot(dialogue):play_dialogue_cue{cue=76}
end)
end
end
local function check(context, state)
if state:variable(p.."phase")=="dialogue_window" and state:variable(p.."window_elapsed")==true
and clear(state) and state:variable(p.."region")==boulevard.region_index and state:variable(p.."ship_present")==true then
request(context,"arming",function()return context:slot(pickup):fire_trigger()end);return
end
if state:variable(p.."phase") then ship_ready(context, state); return end
if clear(state) and state:variable(p.."region") == boulevard.region_index then
if not state:timer(p.."settle") then context:start_timer(p.."settle", 3000) end
else context:cancel_timer(p.."settle") end
end
local function arrive(context, state, region)
if region == nil then return end
context:set_variable(p.."region", region)
if region ~= boulevard.region_index then context:clear_variable(p.."boarding_trigger") end
local phase = state:variable(p.."phase")
if region == cine_state.region_index and phase == "selecting" then
request(context, "starting", function()
return context:slot(cinema):set_cinematic_active{active=true}
end)
elseif region == ship_state.region_index and phase == "returning" then
context:set_variable(p.."phase", "done")
context:cancel_timer(p.."watchdog")
else check(context, state) end
end
local function stop(context, state, reason)
local phase = state:variable(p.."phase")
if phase ~= "starting" and phase ~= "playing" then return end
context:set_variable(p.."end", reason)
request(context, "stopping", function()
return context:slot(cinema):set_cinematic_active{active=false}
end)
end
chain("on_event_player_trigger", function(context, state, event)
if event.slot and event.slot.id == Slot.PT_BAZAAR then
context:set_variable(p.."entered", true)
check(context, state)
elseif event.slot and event.slot.id == pickup
and state:variable(p.."ship_present") == true and state:variable(p.."window_elapsed")==true and clear(state)
and state:variable(p.."region") == boulevard.region_index then
local phase = state:variable(p.."phase")
if phase == "arming" or phase == "boarding" then
context:set_variable(p.."boarding_trigger", true)
board(context, state)
end
end
end)
chain("on_event_object_state", function(context, state, event)
if not event.slot or event.slot.id ~= hawk or event.present == nil then return end
context:set_variable(p.."ship_present", event.present)
if event.present ~= true then context:clear_variable(p.."boarding_trigger") end
check(context, state)
end)
chain("on_event_squad_state", function(context, state, event)
if not event.slot or event.alive_count == nil then return end
for i, id in ipairs(sources) do
if event.slot.id == id then
if event.alive_count > 0 then
context:set_variable(p.."seen/" .. i, true)
context:clear_variable(p.."boarding_trigger")
end
context:set_variable(p.."alive/" .. i, event.alive_count)
check(context, state)
return
end
end
end)
chain("on_event_region_changed", function(context, state, event)
arrive(context, state, event.region_index)
end)
chain("on_event_client_state_changed", function(context, state, event)
if event.entered == true then arrive(context, state, event.held_region_index) end
end)
chain("on_event_timer_elapsed", function(context, state, event)
if event.timer_name == p.."settle" and not state:variable(p.."phase")
and clear(state) and state:variable(p.."region") == boulevard.region_index then
context:clear_variable(p.."ship_present")
context:clear_variable(p.."boarding_trigger")
request(context, "ship_spawning", function()
return context:slot(hawk):set_object_active{active=true}
end)
elseif event.timer_name == p.."dialogue_window" and state:variable(p.."phase")=="dialogue_window" then
context:set_variable(p.."window_elapsed",true)
if state:variable(p.."region")==boulevard.region_index and state:variable(p.."ship_present")==true and clear(state) then
request(context,"arming",function()return context:slot(pickup):fire_trigger()end)
end
elseif event.timer_name == p.."watchdog" then
context:set_variable(p.."error", "Pickup pending; inspect phase, request and actual cinematic events")
end
end)
chain("on_event_effect_result", function(context, state, event)
if not event.request_key or not event.request_key.value
or event.request_key.value ~= state:variable(p.."key") then return end
context:clear_variable(p.."key")
if event.outcome ~= "transport_staged" then
fail(context, "Pickup request " .. tostring(event.outcome))
elseif state:variable(p.."phase") == "ship_spawning" then
context:set_variable(p.."phase", "ship_waiting")
ship_ready(context, state)
elseif state:variable(p.."phase")=="dialogue_dispatch" then
context:set_variable(p.."phase","dialogue_window")
context:start_timer(p.."dialogue_window",window_ms)
elseif state:variable(p.."phase") == "arming" then
context:set_variable(p.."phase", "boarding")
context:cancel_timer(p.."watchdog")
board(context, state)
elseif state:variable(p.."phase") == "disarming" then
request(context, "selecting", function() return context:select_state(cine_state) end)
elseif state:variable(p.."phase") == "stopping" then
request(context, "returning", function() return context:select_state(ship_state) end)
end
end)
chain("on_event_cinematic_started", function(context, state, event)
if event.slot and event.slot.id == cinema
and state:variable(p.."phase") == "starting" then
context:set_variable(p.."phase", "playing")
context:set_variable(p.."started", true)
context:cancel_timer(p.."watchdog")
end
end)
chain("on_event_cinematic_terminated", function(context, state, event)
if not event.slot or event.slot.id ~= cinema then return end
if state:variable(p.."phase") == "playing" then stop(context, state, "terminated")
elseif state:variable(p.."phase") == "starting" then
fail(context, "Cinematic terminated before a start report; no completion inferred")
end
end)
chain("on_event_cinematic_skip_requested", function(context, state, event)
if event.slot and event.slot.id == cinema then stop(context, state, "player_skip") end
end)
chain("on_load", function(context, state)
if state:variable(p.."policy")~=37 then
local phase=state:variable(p.."phase")
if phase=="arming" or phase=="boarding" then
context:set_variable(p.."window_elapsed",true)
context:set_variable(p.."error","Legacy pickup retained; fresh mission uses arrival dialogue window")
end
context:set_variable(p.."policy",37)
end
check(context, state)
end)
end
install_amanda_pickup(program)


local function install_ikora_collision_handoff(program)
local p="hc37.ikora_collision."
local blockers={Slot.O_INVISIBLE_CHARACTER_BLOCKER,Slot.O_INVISIBLE_BLOCKER_FOR_IKORA}
local function field(i,key)return p..i..key end
local function eligible(s)
return s:variable("hc13.region")==0 and s:variable("hc26.g.bazaar.done")==true
and s:variable("hc13.bazaar_door.state")==3
end
local function pump(c,s)
if not eligible(s) then return end
local complete=true
for i,id in ipairs(blockers) do
local key=field(i,"key")
if s:variable(field(i,"staged"))==true and s:variable(field(i,"absent"))==true then
c:set_variable(field(i,"done"),true)
end
if s:variable(field(i,"done"))~=true then
complete=false
if not s:variable(key) and (s:variable(field(i,"tries")) or 0)<3 then
c:set_variable(field(i,"tries"),(s:variable(field(i,"tries")) or 0)+1)
local ok,result=pcall(function()return c:slot(id):set_object_active{active=false}end)
if ok and result and result.value then c:set_variable(key,result.value)
else c:set_variable(p.."error",string.sub(tostring(result),1,127)) end
end
end
end
c:set_variable(p.."done",complete)
if complete then c:cancel_timer(p.."watch");c:clear_variable(p.."error")
elseif not s:timer(p.."watch") then c:start_timer(p.."watch",5000) end
end
local function chain(name,fn)
local base=program[name]
program[name]=function(c,s,e)
if e and e.attempt_generation and e.attempt_generation~=c.attempt_generation then return end
if base then base(c,s,e) end
fn(c,s,e or {})
end
end
for _,name in ipairs{"on_load","on_event_region_changed","on_event_client_state_changed",
"on_event_device_state","on_event_squad_state"} do chain(name,pump) end
chain("on_event_timer_elapsed",function(c,s,e)
if e.timer_name==p.."watch" and s:variable(p.."done")~=true then
c:set_variable(p.."error","Awaiting blocker absence; pending removal is not repeated")
end
pump(c,s)
end)
chain("on_event_effect_result",function(c,s,e)
for i in ipairs(blockers) do
if e.request_key and e.request_key.value==s:variable(field(i,"key")) then
if e.outcome=="transport_staged" then c:set_variable(field(i,"staged"),true)
else
c:clear_variable(field(i,"key"));c:clear_variable(field(i,"staged"))
c:set_variable(p.."error","Blocker removal "..tostring(e.outcome))
end
end
end
pump(c,s)
end)
chain("on_event_object_state",function(c,s,e)
if e.slot and e.present~=nil and e.generation~=nil then
for i,id in ipairs(blockers) do
if e.slot.id==id and e.generation>=(s:variable(field(i,"generation")) or 0) then
c:set_variable(field(i,"generation"),e.generation)
c:set_variable(field(i,"absent"),e.present==false)
if e.present and s:variable(field(i,"done"))==true then
for _,key in ipairs{"done","key","staged","tries"} do c:clear_variable(field(i,key)) end
c:clear_variable(p.."done")
end
end
end
end
pump(c,s)
end)
end
install_ikora_collision_handoff(program)

audio.install(program)
-- Reconstructed presentation policy: start an authored combat section on Plaza
-- approach. Section1 is authenticated in80B5090F; its FNV name matches first_cabal.
-- The original host's complete score mapping has not been recovered.
do
local p="hc30.music."
local function pump(c,s)
local wanted=s:variable(p.."wanted")
if wanted==nil or wanted==s:variable(p.."staged") or s:variable(p.."key") then return end
local tries=(s:variable(p.."tries") or 0)+1
if tries>3 then return end
c:set_variable(p.."tries",tries)
local ok,k=pcall(function()return c:slot(Slot.M_MUSIC_SENSOR_80B50913):set_music_section{section=1,enabled=wanted}end)
if ok and k and k.value then c:set_variable(p.."key",k.value);c:set_variable(p.."sent",wanted)
else c:set_variable(p.."error",string.sub(tostring(k),1,127));if tries<3 then c:start_timer(p.."retry",1000)end end
end
local function select(c,s,enabled)
if s:variable(p.."wanted")~=enabled then c:set_variable(p.."wanted",enabled);c:clear_variable(p.."tries") end
pump(c,s)
end
local function chain(name,fn)
local old=program[name]
program[name]=function(c,s,e)
if e and e.attempt_generation and e.attempt_generation~=c.attempt_generation then return end
if old then old(c,s,e)end;fn(c,s,e or {})
end
end
chain("on_event_player_trigger",function(c,s,e)
local id=e.slot and e.slot.id
if id==Slot.PT_GOTO_PLAZA_80B50B91 or id==Slot.PT_GOTO_PLAZA_80B51058 or id==Slot.PT_PLAZA_SPAWN_INIT then select(c,s,true)end
end)
local function region(c,s,e)
local r=e.region_index
if e.entered==false then return end
if e.entered then r=e.held_region_index or e.current_region_index or r end
if r==48 then select(c,s,true)
elseif r~=nil and r~=32 and s:variable(p.."wanted")==true then select(c,s,false)end
end
chain("on_event_region_changed",region);chain("on_event_client_state_changed",region)
chain("on_event_effect_result",function(c,s,e)
if not e.request_key or e.request_key.value~=s:variable(p.."key") then return end
c:clear_variable(p.."key")
if e.outcome=="transport_staged" then
c:set_variable(p.."staged",s:variable(p.."sent"));c:clear_variable(p.."tries");c:clear_variable(p.."error");pump(c,s)
else c:set_variable(p.."error",tostring(e.outcome));if (s:variable(p.."tries")or 0)<3 then c:start_timer(p.."retry",1000)end end
end)
chain("on_load",pump)
chain("on_event_timer_elapsed",function(c,s,e)if e.timer_name==p.."retry" then pump(c,s)end end)
end

return program
