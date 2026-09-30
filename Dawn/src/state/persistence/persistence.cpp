#include "persistence.h"

#include <Windows.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "../../../vendor/sqlite/sqlite3.h"

namespace dawn::state::persistence {
namespace {

constexpr int kSchemaVersion = 5;
/** Additive roll storage; old instances remain curated and are never rerolled on load. */
constexpr const char* kRollSchema = R"sql(
CREATE TABLE IF NOT EXISTS item_rolls(instance_soid TEXT PRIMARY KEY, entropy BLOB NOT NULL CHECK(length(entropy)=8), lane_mask INTEGER NOT NULL CHECK(lane_mask>=0 AND lane_mask<4096), owned_rows BLOB NOT NULL CHECK(length(owned_rows)=96), FOREIGN KEY(instance_soid) REFERENCES character_items(instance_soid) ON DELETE CASCADE);
)sql";

constexpr std::wstring_view kDatabaseName = L"\\player-state.db";
constexpr std::uint64_t kFirstGeneratedItemSoid = 0x4000000000000001ULL;
constexpr std::uint64_t kFirstProfileItemSoid = 0x5000000000000001ULL;

sqlite3* database{};
SRWLOCK databaseLock{SRWLOCK_INIT};
std::int64_t accountRevision{};
std::uint64_t rewardEpoch{};
std::uint64_t nextItemSoid{kFirstGeneratedItemSoid};
std::uint64_t nextProfileItemSoid{kFirstProfileItemSoid};
bool memoryOnly{};

class Lock final {
public:
    Lock() noexcept { AcquireSRWLockExclusive(&databaseLock); }
    ~Lock() noexcept { ReleaseSRWLockExclusive(&databaseLock); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};

class Statement final {
public:
    sqlite3_stmt* value{};
    explicit Statement(const char* sql) noexcept {
        if (database != nullptr) {
            (void)sqlite3_prepare_v2(database, sql, -1, &value, nullptr);
        }
    }
    ~Statement() noexcept { (void)sqlite3_finalize(value); }
    [[nodiscard]] bool ready() const noexcept { return value != nullptr; }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

void log_failure(std::string_view operation) noexcept {
    std::array<char, 320> line{};
    const char* detail = database == nullptr ? "database unavailable" : sqlite3_errmsg(database);
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=persistence operation=%.*s result=fail sqlite=%d detail=%s",
                                      static_cast<int>(operation.size()),
                                      operation.data(),
                                      database == nullptr ? -1 : sqlite3_errcode(database),
                                      detail == nullptr ? "unknown" : detail);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::error,
                         {line.data(), (std::min)(line.size() - 1U,
                                                  static_cast<std::size_t>(written))});
    }
}

[[nodiscard]] bool execute(const char* sql) noexcept {
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
    return result == SQLITE_OK;
}

void delete_database_files(const core::path::Buffer& path) noexcept {
    (void)DeleteFileW(path.chars.data());
    core::path::Buffer sidecar=path;
    if(core::path::append(sidecar,L"-wal"))(void)DeleteFileW(sidecar.chars.data());
    sidecar=path;
    if(core::path::append(sidecar,L"-shm"))(void)DeleteFileW(sidecar.chars.data());
}

[[nodiscard]] bool begin() noexcept { return execute("BEGIN IMMEDIATE"); }
[[nodiscard]] bool commit() noexcept { return execute("COMMIT"); }
void rollback() noexcept { (void)execute("ROLLBACK"); }

[[nodiscard]] bool format_u64(std::uint64_t value, std::array<char, 17>& text) noexcept {
    const int written = std::snprintf(text.data(), text.size(), "%016llX",
                                      static_cast<unsigned long long>(value));
    return written == 16;
}

[[nodiscard]] bool parse_u64(const unsigned char* text, std::uint64_t& value) noexcept {
    if (text == nullptr) return false;
    const char* first = reinterpret_cast<const char*>(text);
    const std::size_t length = std::strlen(first);
    if (length != 16) return false;
    const auto parsed = std::from_chars(first, first + length, value, 16);
    return parsed.ec == std::errc{} && parsed.ptr == first + length;
}

[[nodiscard]] bool bind_u64(sqlite3_stmt* statement, int index, std::uint64_t value) noexcept {
    std::array<char, 17> text{};
    return format_u64(value, text)
           && sqlite3_bind_text(statement, index, text.data(), 16, SQLITE_TRANSIENT) == SQLITE_OK;
}

[[nodiscard]] bool bind_i64(sqlite3_stmt* statement, int index, std::int64_t value) noexcept {
    return sqlite3_bind_int64(statement, index, value) == SQLITE_OK;
}

[[nodiscard]] bool step_done(sqlite3_stmt* statement) noexcept {
    return sqlite3_step(statement) == SQLITE_DONE;
}

[[nodiscard]] bool column_i32(sqlite3_stmt* statement,int column,std::int32_t& value) noexcept {
    if(sqlite3_column_type(statement,column)!=SQLITE_INTEGER)return false;
    const auto wide=sqlite3_column_int64(statement,column);
    if(wide<(std::numeric_limits<std::int32_t>::min)()||wide>(std::numeric_limits<std::int32_t>::max)())return false;
    value=static_cast<std::int32_t>(wide);return true;
}

[[nodiscard]] int scope_value(Scope scope) noexcept { return static_cast<int>(scope); }

// Called inside the same transaction as the load/import. A failed read rolls back
// the schema upgrade too, leaving an older save usable by the previous build.
[[nodiscard]] bool columns_match(const char* sql,
                                std::initializer_list<std::string_view> expected) noexcept {
    Statement columns{sql};
    if (!columns.ready()) return false;
    for (const auto name : expected) {
        if (sqlite3_step(columns.value) != SQLITE_ROW) return false;
        const auto* text = sqlite3_column_text(columns.value, 1);
        if (!text || name != reinterpret_cast<const char*>(text)) return false;
    }
    return sqlite3_step(columns.value) == SQLITE_DONE;
}

[[nodiscard]] bool migrate_vendor_v2() noexcept {
    // Two branches shipped user_version=2 with different vendor layouts. Inspect
    // both tables before upgrading; version 3 unambiguously uses the vendor branch codec.
    if (columns_match("PRAGMA table_info(vendor_progress)",
                      {"owner_soid", "position", "vendor", "points", "rewards"})
        && columns_match("PRAGMA table_info(vendor_unlocks)",
                         {"owner_soid", "kind", "position", "slot", "value"})) return true;
    if (!columns_match("PRAGMA table_info(vendor_progress)",
                       {"scope", "owner_soid", "position", "vendor", "points", "rewards"})
        || !columns_match("PRAGMA table_info(vendor_unlocks)",
                          {"scope", "owner_soid", "numeric", "position", "slot", "value"})) return false;

    // Scope becomes implicit in the unique owner SOID. Validate it before dropping
    // the column, including the empty progress slots that the old writer emitted.
    {
        Statement invalid{R"sql(
SELECT 1 FROM (
 SELECT scope,owner_soid FROM vendor_progress
 UNION ALL SELECT scope,owner_soid FROM vendor_unlocks
) WHERE typeof(scope)!='integer' OR NOT (
 (scope=0 AND owner_soid IN (SELECT primary_soid FROM account WHERE id=1)) OR
 (scope=1 AND owner_soid IN (SELECT soid FROM characters)))
UNION ALL
SELECT 1 FROM vendor_progress
 WHERE typeof(position)!='integer' OR position NOT BETWEEN 0 AND 15
 OR (vendor=65535 AND (typeof(vendor)!='integer' OR typeof(points)!='integer'
     OR typeof(rewards)!='integer' OR points!=0 OR rewards!=0))
LIMIT 1
)sql"};
        if (!invalid.ready() || sqlite3_step(invalid.value) != SQLITE_DONE) return false;
    }
    return execute(R"sql(
CREATE TABLE vendor_progress_v3(owner_soid TEXT NOT NULL, position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 15), vendor INTEGER NOT NULL CHECK(vendor BETWEEN 0 AND 65534), points INTEGER NOT NULL CHECK(points>=0), rewards INTEGER NOT NULL CHECK(rewards>=0), PRIMARY KEY(owner_soid,position), UNIQUE(owner_soid,vendor));
CREATE TABLE vendor_unlocks_v3(owner_soid TEXT NOT NULL, kind INTEGER NOT NULL CHECK(kind IN(0,1)), position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 2047), slot INTEGER NOT NULL CHECK(slot BETWEEN 0 AND 65535), value INTEGER NOT NULL, PRIMARY KEY(owner_soid,kind,position), UNIQUE(owner_soid,kind,slot));
INSERT INTO vendor_progress_v3 SELECT owner_soid,position,CASE vendor WHEN 324 THEN 11 ELSE vendor END,points,rewards FROM vendor_progress WHERE vendor!=65535;
INSERT INTO vendor_unlocks_v3 SELECT owner_soid,numeric,position,slot,value FROM vendor_unlocks;
DROP TABLE vendor_progress;
DROP TABLE vendor_unlocks;
ALTER TABLE vendor_progress_v3 RENAME TO vendor_progress;
ALTER TABLE vendor_unlocks_v3 RENAME TO vendor_unlocks;
)sql");
}

[[nodiscard]] bool migrate_schema(int version) noexcept {
    if (version == kSchemaVersion) return true;
    if (version == 4) return execute(R"sql(
INSERT OR IGNORE INTO settings_values(key,integer_value) VALUES
('display.motionBlur',0),('display.filmGrain',0),('display.chromaticAberration',0),
('pc.seedVersion',0),('pc.verticalSyncMode',0),('pc.fieldOfViewAdjustment',0),
('pc.useLocalKeyBindings',0);
INSERT OR IGNORE INTO settings_values(key,integer_value)
SELECT 'pc.voiceChatEnabled',integer_value FROM settings_values WHERE key='social.voiceChatEnabled';
PRAGMA user_version=5;
)sql");
    if (version == 3) return execute(kRollSchema) && migrate_schema(4);
    if (version == 2) return migrate_vendor_v2() && migrate_schema(3);
    if (version != 1) return false;
    return execute(R"sql(
ALTER TABLE characters ADD COLUMN vendor_campaigns INTEGER NOT NULL DEFAULT 0 CHECK(vendor_campaigns BETWEEN 0 AND 7);
ALTER TABLE character_items ADD COLUMN postmaster INTEGER NOT NULL DEFAULT 0 CHECK(postmaster IN(0,1));
CREATE TABLE vendor_progress(owner_soid TEXT NOT NULL, position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 15), vendor INTEGER NOT NULL CHECK(vendor BETWEEN 0 AND 65534), points INTEGER NOT NULL CHECK(points>=0), rewards INTEGER NOT NULL CHECK(rewards>=0), PRIMARY KEY(owner_soid,position), UNIQUE(owner_soid,vendor));
CREATE TABLE vendor_unlocks(owner_soid TEXT NOT NULL, kind INTEGER NOT NULL CHECK(kind IN(0,1)), position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 2047), slot INTEGER NOT NULL CHECK(slot BETWEEN 0 AND 65535), value INTEGER NOT NULL, PRIMARY KEY(owner_soid,kind,position), UNIQUE(owner_soid,kind,slot));
PRAGMA user_version=3;
)sql") && migrate_schema(3);
}

[[nodiscard]] bool create_schema() noexcept {
    static constexpr const char* sql = R"sql(
CREATE TABLE metadata(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
INSERT INTO metadata(key,value) VALUES('account_revision',0),('legacy_import_complete',0),('reward_epoch',1);
CREATE TABLE account(id INTEGER PRIMARY KEY CHECK(id=1), primary_soid TEXT NOT NULL);
CREATE TABLE dismantle_rewards(position INTEGER PRIMARY KEY, definition_hash INTEGER NOT NULL, quantity INTEGER NOT NULL);
CREATE TABLE profile_items(position INTEGER PRIMARY KEY, instance_soid TEXT NOT NULL, definition_hash INTEGER NOT NULL, quantity INTEGER NOT NULL, mutation_serial INTEGER NOT NULL);
CREATE TABLE characters(position INTEGER PRIMARY KEY, soid TEXT NOT NULL UNIQUE, last_selected INTEGER NOT NULL, race INTEGER NOT NULL, gender INTEGER NOT NULL, class INTEGER NOT NULL, level INTEGER NOT NULL, accepted INTEGER NOT NULL, preview_available INTEGER NOT NULL, appearance REAL NOT NULL, last_destination INTEGER NOT NULL, content_bypass INTEGER NOT NULL, movement_ability INTEGER NOT NULL, grenade_ability INTEGER NOT NULL, super_ability INTEGER NOT NULL, melee_ability INTEGER NOT NULL, class_ability INTEGER NOT NULL, next_inventory_serial INTEGER NOT NULL);
CREATE TABLE character_items(character_soid TEXT NOT NULL, location INTEGER NOT NULL, position INTEGER NOT NULL, instance_soid TEXT NOT NULL UNIQUE, definition_hash INTEGER NOT NULL, level INTEGER NOT NULL, quantity INTEGER NOT NULL, mutation_serial INTEGER NOT NULL, flags INTEGER NOT NULL, socket_policy INTEGER NOT NULL, PRIMARY KEY(character_soid,location,position), FOREIGN KEY(character_soid) REFERENCES characters(soid) DEFERRABLE INITIALLY DEFERRED);
CREATE TABLE item_sockets(instance_soid TEXT NOT NULL, lane INTEGER NOT NULL, plug_hash INTEGER, PRIMARY KEY(instance_soid,lane), FOREIGN KEY(instance_soid) REFERENCES character_items(instance_soid) ON DELETE CASCADE);
CREATE TABLE settings_values(key TEXT PRIMARY KEY, integer_value INTEGER, real_value REAL, CHECK((integer_value IS NULL)!=(real_value IS NULL)));
CREATE TABLE key_bindings(action INTEGER PRIMARY KEY, primary_input INTEGER, secondary_input INTEGER);
CREATE TABLE allocators(name TEXT PRIMARY KEY, next_value TEXT NOT NULL);
CREATE TABLE durable_flags(scope INTEGER NOT NULL, owner_soid TEXT NOT NULL, slot INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(scope,owner_soid,slot));
CREATE TABLE durable_objectives(scope INTEGER NOT NULL, owner_soid TEXT NOT NULL, slot INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(scope,owner_soid,slot));
CREATE TABLE durable_progressions(scope INTEGER NOT NULL, owner_soid TEXT NOT NULL, definition_index INTEGER NOT NULL, lane INTEGER NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(scope,owner_soid,definition_index,lane));
CREATE TABLE family5_flags(slot INTEGER PRIMARY KEY, value INTEGER NOT NULL);
CREATE TABLE family5_values(slot INTEGER PRIMARY KEY, value INTEGER NOT NULL);
CREATE TABLE missions(character_soid TEXT NOT NULL, mission_hash INTEGER NOT NULL, checkpoint_hash INTEGER NOT NULL, checkpoint_slice_set INTEGER NOT NULL, activity_index INTEGER NOT NULL, progress INTEGER NOT NULL, completed INTEGER NOT NULL, updated_utc INTEGER NOT NULL, PRIMARY KEY(character_soid,mission_hash), FOREIGN KEY(character_soid) REFERENCES characters(soid) DEFERRABLE INITIALLY DEFERRED);
CREATE TABLE reward_debts(debt_id INTEGER PRIMARY KEY AUTOINCREMENT, account_soid TEXT NOT NULL, character_soid TEXT NOT NULL, mission_hash INTEGER NOT NULL, runtime_epoch TEXT NOT NULL, session_id TEXT NOT NULL, run_id TEXT NOT NULL, definition_hash INTEGER NOT NULL, quantity INTEGER NOT NULL CHECK(quantity>0), credited INTEGER NOT NULL DEFAULT 0 CHECK(credited>=0 AND credited<=quantity), delivered INTEGER NOT NULL DEFAULT 0 CHECK(delivered IN(0,1)), UNIQUE(account_soid,runtime_epoch,session_id,run_id,definition_hash));
PRAGMA user_version=2;
)sql";
    return execute(sql) && migrate_schema(1);
}

[[nodiscard]] bool write_setting_integer(const char* key, std::int64_t value) noexcept {
    Statement statement{"INSERT INTO settings_values(key,integer_value) VALUES(?1,?2)"};
    return statement.ready() && sqlite3_bind_text(statement.value,1,key,-1,SQLITE_STATIC)==SQLITE_OK
           && bind_i64(statement.value,2,value) && step_done(statement.value);
}

[[nodiscard]] bool write_setting_real(const char* key, double value) noexcept {
    Statement statement{"INSERT INTO settings_values(key,real_value) VALUES(?1,?2)"};
    return statement.ready() && sqlite3_bind_text(statement.value,1,key,-1,SQLITE_STATIC)==SQLITE_OK
           && sqlite3_bind_double(statement.value,2,value)==SQLITE_OK && step_done(statement.value);
}

template <typename T>
[[nodiscard]] bool assign_integer(std::int64_t source,T& target) noexcept {
    if constexpr(std::is_same_v<T,bool>) {
        if(source!=0&&source!=1)return false;
    } else if(source<static_cast<std::int64_t>((std::numeric_limits<T>::min)())
              ||source>static_cast<std::int64_t>((std::numeric_limits<T>::max)()))return false;
    target=static_cast<T>(source);return true;
}

#include "vendor_state.inl"

#define WRITE_I(group, field) if (!write_setting_integer(#group "." #field, settings.group.field)) return false
#define WRITE_R(group, field) if (!write_setting_real(#group "." #field, settings.group.field)) return false

[[nodiscard]] bool write_settings(const account::settings::AccountSettings& settings) noexcept {
    if (!execute("DELETE FROM settings_values;DELETE FROM key_bindings")) return false;
    if (!write_setting_integer("configured", settings.configured)) return false;
    WRITE_I(controls,buttonLayout); WRITE_I(controls,movementMode);
    WRITE_I(controls,controllerLookSensitivity); WRITE_I(controls,controllerInvertVertical);
    WRITE_I(controls,controllerAutoLookCentering); WRITE_I(controls,controllerVibration);
    WRITE_I(controls,controllerSwapShoulders); WRITE_I(controls,controllerInvertHorizontal);
    WRITE_I(controls,mouseLookSensitivity); WRITE_I(controls,mouseInvertVertical);
    WRITE_I(controls,mouseInvertHorizontal); WRITE_I(controls,unidentifiedToggle);
    WRITE_I(controls,mouseAimSmoothing); WRITE_R(controls,adsSensitivityModifier);
    WRITE_I(controls,doublePressDelay);
    WRITE_I(audio,voiceOutputMode); WRITE_I(audio,teamVoiceChannel); WRITE_I(audio,reservedMode);
    WRITE_I(audio,migrationVersion); WRITE_I(audio,chatVolume); WRITE_I(audio,muteWhenUnfocused);
    WRITE_I(audio,soundEffectsVolume); WRITE_I(audio,dialogueVolume); WRITE_I(audio,musicVolume);
    WRITE_I(display,brightness); WRITE_I(display,showFps); WRITE_I(display,hdrMode);
    WRITE_R(display,calibrationPrimary); WRITE_R(display,calibrationAlpha);
    WRITE_I(display,motionBlur); WRITE_I(display,filmGrain); WRITE_I(display,chromaticAberration);
    WRITE_I(pc,seedVersion); WRITE_I(pc,voiceChatEnabled); WRITE_I(pc,verticalSyncMode);
    WRITE_I(pc,fieldOfViewAdjustment); WRITE_I(pc,useLocalKeyBindings);
    WRITE_I(interface,subtitlesMode); WRITE_I(interface,colorblindMode); WRITE_I(interface,helmetMode);
    WRITE_I(interface,hudOpacity); WRITE_I(interface,displayHints); WRITE_I(interface,backgroundOpacity);
    WRITE_I(interface,reticleLocation); WRITE_I(interface,reticleColor); WRITE_I(interface,textSize);
    WRITE_I(interface,textColor); WRITE_I(interface,textBackgroundStyle);
    WRITE_I(interface,textBackgroundOpacity); WRITE_I(interface,reservedTextMode);
    WRITE_I(interface,subtitleOptionsEntry);
    WRITE_I(social,preferGoodConnection); WRITE_I(social,textChatMode); WRITE_I(social,showRealNames);
    WRITE_I(social,clanInviteNotifications); WRITE_I(social,profanityFilter);
    WRITE_I(social,voiceChatEnabled); WRITE_I(social,whisperChatMode);
    WRITE_I(social,teamChatJoinMode); WRITE_I(social,localChatJoinMode);
    WRITE_I(social,clanChatJoinMode); WRITE_I(social,chatAutoHideMode);
    if (!write_setting_integer("keyBindings.configured", settings.keyBindings.configured)) return false;
    Statement binding{"INSERT INTO key_bindings(action,primary_input,secondary_input) VALUES(?1,?2,?3)"};
    if (!binding.ready()) return false;
    for (std::size_t i=0;i<settings.keyBindings.values.size();++i) {
        const auto& value=settings.keyBindings.values[i];
        sqlite3_reset(binding.value); sqlite3_clear_bindings(binding.value);
        if (sqlite3_bind_int(binding.value,1,static_cast<int>(i))!=SQLITE_OK
            || (value.primary ? sqlite3_bind_int(binding.value,2,*value.primary) : sqlite3_bind_null(binding.value,2))!=SQLITE_OK
            || (value.secondary ? sqlite3_bind_int(binding.value,3,*value.secondary) : sqlite3_bind_null(binding.value,3))!=SQLITE_OK
            || !step_done(binding.value)) return false;
    }
    return true;
}

#undef WRITE_I
#undef WRITE_R

[[nodiscard]] bool write_item(sqlite3_stmt* itemStatement,
                              sqlite3_stmt* socketStatement,
                              std::uint64_t characterSoid,
                              int location,
                              std::size_t position,
                              const account::inventory::Item& item) noexcept {
    sqlite3_reset(itemStatement); sqlite3_clear_bindings(itemStatement);
    if (!bind_u64(itemStatement,1,characterSoid) || sqlite3_bind_int(itemStatement,2,location)!=SQLITE_OK
        || bind_i64(itemStatement,3,static_cast<std::int64_t>(position))==false
        || !bind_u64(itemStatement,4,item.instanceSoid)
        || bind_i64(itemStatement,5,item.definitionHash)==false || sqlite3_bind_int(itemStatement,6,item.level)!=SQLITE_OK
        || sqlite3_bind_int(itemStatement,7,item.quantity)!=SQLITE_OK || sqlite3_bind_int(itemStatement,8,item.mutationSerial)!=SQLITE_OK
        || bind_i64(itemStatement,9,item.flags)==false || sqlite3_bind_int(itemStatement,10,static_cast<int>(item.sockets.policy))!=SQLITE_OK
        || sqlite3_bind_int(itemStatement,11,item.postmaster)!=SQLITE_OK
        || !step_done(itemStatement)) return false;
    // The roll and its offered perk rows commit atomically with the item and selected sockets.
    Statement roll{"INSERT INTO item_rolls VALUES(?1,?2,?3,?4)"};
    if (!roll.ready() || !bind_u64(roll.value, 1, item.instanceSoid)
        || sqlite3_bind_blob(roll.value, 2, item.randomRoll.data(),
                             static_cast<int>(sizeof(item.randomRoll)), SQLITE_TRANSIENT) != SQLITE_OK
        || !bind_i64(roll.value, 3, item.rolledLaneMask)
        || sqlite3_bind_blob(roll.value, 4, item.availablePlugRows.data(),
                             static_cast<int>(sizeof(item.availablePlugRows)), SQLITE_TRANSIENT) != SQLITE_OK
        || !step_done(roll.value)) return false;
    for (std::size_t lane=0;lane<item.sockets.plugCount;++lane) {
        sqlite3_reset(socketStatement); sqlite3_clear_bindings(socketStatement);
        if (!bind_u64(socketStatement,1,item.instanceSoid) || sqlite3_bind_int(socketStatement,2,static_cast<int>(lane))!=SQLITE_OK
            || (item.sockets.plugs[lane] ? bind_i64(socketStatement,3,*item.sockets.plugs[lane])
                                         : sqlite3_bind_null(socketStatement,3)==SQLITE_OK)==false
            || !step_done(socketStatement)) return false;
    }
    return true;
}

[[nodiscard]] std::uint64_t successor(std::uint64_t value) noexcept {
    return value == (std::numeric_limits<std::uint64_t>::max)() ? value : value + 1U;
}

void observe_allocators(const AccountState& account) noexcept {
    nextItemSoid=(std::max)(nextItemSoid,kFirstGeneratedItemSoid);
    nextProfileItemSoid=(std::max)(nextProfileItemSoid,kFirstProfileItemSoid);
    for(std::size_t i=0;i<account.profileItemCount;++i) {
        if(account.profileItems[i].instanceSoid>=kFirstProfileItemSoid)
            nextProfileItemSoid=(std::max)(nextProfileItemSoid,successor(account.profileItems[i].instanceSoid));
    }
    for(std::size_t c=0;c<account.characterCount;++c) {
        const auto& character=account.characters[c];
        for(const auto& slot:character.equipment.slots) if(slot && slot->instanceSoid>=kFirstGeneratedItemSoid)
            nextItemSoid=(std::max)(nextItemSoid,successor(slot->instanceSoid));
        for(std::size_t i=0;i<character.inventory.count;++i) if(character.inventory.values[i].instanceSoid>=kFirstGeneratedItemSoid)
            nextItemSoid=(std::max)(nextItemSoid,successor(character.inventory.values[i].instanceSoid));
    }
}

[[nodiscard]] bool write_allocator(const char* name,std::uint64_t value) noexcept {
    Statement statement{"INSERT INTO allocators(name,next_value) VALUES(?1,?2) ON CONFLICT(name) DO UPDATE SET next_value=excluded.next_value"};
    return statement.ready() && sqlite3_bind_text(statement.value,1,name,-1,SQLITE_STATIC)==SQLITE_OK
           && bind_u64(statement.value,2,value) && step_done(statement.value);
}

[[nodiscard]] bool write_account(const AccountState& accountState) noexcept {
    if (!account::valid(accountState)
        || !execute("DELETE FROM vendor_unlocks;DELETE FROM vendor_progress;DELETE FROM item_sockets;DELETE FROM character_items;DELETE FROM characters;DELETE FROM profile_items;DELETE FROM dismantle_rewards;DELETE FROM account")) return false;
    Statement accountInsert{"INSERT INTO account(id,primary_soid) VALUES(1,?1)"};
    if(!accountInsert.ready() || !bind_u64(accountInsert.value,1,accountState.primarySoid) || !step_done(accountInsert.value)) return false;
    Statement reward{"INSERT INTO dismantle_rewards(position,definition_hash,quantity) VALUES(?1,?2,?3)"};
    for(std::size_t i=0;i<accountState.dismantleRewardCount;++i) {
        sqlite3_reset(reward.value);sqlite3_clear_bindings(reward.value);
        if(!reward.ready() || bind_i64(reward.value,1,i)==false || bind_i64(reward.value,2,accountState.dismantleRewards[i].definitionHash)==false
            || sqlite3_bind_int(reward.value,3,accountState.dismantleRewards[i].quantity)!=SQLITE_OK || !step_done(reward.value)) return false;
    }
    Statement profile{"INSERT INTO profile_items(position,instance_soid,definition_hash,quantity,mutation_serial) VALUES(?1,?2,?3,?4,?5)"};
    for(std::size_t i=0;i<accountState.profileItemCount;++i) {
        const auto& p=accountState.profileItems[i]; sqlite3_reset(profile.value);sqlite3_clear_bindings(profile.value);
        if(!profile.ready() || !bind_i64(profile.value,1,i) || !bind_u64(profile.value,2,p.instanceSoid)
            || !bind_i64(profile.value,3,p.definitionHash) || sqlite3_bind_int(profile.value,4,p.quantity)!=SQLITE_OK
            || sqlite3_bind_int(profile.value,5,p.mutationSerial)!=SQLITE_OK || !step_done(profile.value)) return false;
    }
    Statement character{"INSERT INTO characters VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19)"};
    Statement item{"INSERT INTO character_items VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)"};
    Statement socket{"INSERT INTO item_sockets VALUES(?1,?2,?3)"};
    if(!character.ready()||!item.ready()||!socket.ready()) return false;
    for(std::size_t i=0;i<accountState.characterCount;++i) {
        const auto& c=accountState.characters[i]; sqlite3_reset(character.value);sqlite3_clear_bindings(character.value);
        if(!bind_i64(character.value,1,i)||!bind_u64(character.value,2,c.soid)
            ||sqlite3_bind_int(character.value,3,c.selected)!=SQLITE_OK||sqlite3_bind_int(character.value,4,static_cast<int>(c.race))!=SQLITE_OK
            ||sqlite3_bind_int(character.value,5,static_cast<int>(c.gender))!=SQLITE_OK||sqlite3_bind_int(character.value,6,static_cast<int>(c.characterClass))!=SQLITE_OK
            ||sqlite3_bind_int(character.value,7,c.level)!=SQLITE_OK||sqlite3_bind_int(character.value,8,c.accepted)!=SQLITE_OK
            ||sqlite3_bind_int(character.value,9,c.previewAvailable)!=SQLITE_OK||sqlite3_bind_double(character.value,10,c.appearanceValue)!=SQLITE_OK
            ||!bind_i64(character.value,11,c.lastOrbitedDestination)||sqlite3_bind_int(character.value,12,c.contentBypass)!=SQLITE_OK
            ||sqlite3_bind_int(character.value,13,c.movementAbilityEntry)!=SQLITE_OK||sqlite3_bind_int(character.value,14,c.grenadeAbilityEntry)!=SQLITE_OK
            ||sqlite3_bind_int(character.value,15,c.superAbilityEntry)!=SQLITE_OK||sqlite3_bind_int(character.value,16,c.meleeAbilityEntry)!=SQLITE_OK
            ||sqlite3_bind_int(character.value,17,c.classAbilityEntry)!=SQLITE_OK||!bind_i64(character.value,18,c.nextInventorySerial)
            ||sqlite3_bind_int(character.value,19,c.vendorCampaigns)!=SQLITE_OK
            ||!step_done(character.value)) return false;
        for(std::size_t slot=0;slot<c.equipment.slots.size();++slot) if(c.equipment.slots[slot]
            && !write_item(item.value,socket.value,c.soid,0,slot,*c.equipment.slots[slot])) return false;
        for(std::size_t row=0;row<c.inventory.count;++row)
            if(!write_item(item.value,socket.value,c.soid,1,row,c.inventory.values[row])) return false;
    }
    if(!write_vendor_state(accountState) || !write_settings(accountState.settings)) return false;
    observe_allocators(accountState);
    return write_allocator("item_instance",nextItemSoid)
           && write_allocator("profile_item_instance",nextProfileItemSoid);
}

[[nodiscard]] bool read_settings(account::settings::AccountSettings& settings) noexcept {
    settings = {};
    Statement rows{"SELECT key,integer_value,real_value FROM settings_values ORDER BY key"};
    if (!rows.ready()) return false;
    std::size_t count=0;int result{};
    while((result=sqlite3_step(rows.value))==SQLITE_ROW) {
        const char* key=reinterpret_cast<const char*>(sqlite3_column_text(rows.value,0));
        if(key==nullptr) return false;
        const bool integer=sqlite3_column_type(rows.value,1)==SQLITE_INTEGER&&sqlite3_column_type(rows.value,2)==SQLITE_NULL;
        const bool real=sqlite3_column_type(rows.value,1)==SQLITE_NULL&&sqlite3_column_type(rows.value,2)==SQLITE_FLOAT;
        if(!integer&&!real)return false;
        const std::int64_t i=sqlite3_column_int64(rows.value,1);
        const double r=sqlite3_column_double(rows.value,2);
        bool matched=true;
#define READ_I(name, target) if(std::strcmp(key,name)==0) { if(!integer||!assign_integer(i,target)) return false; }
#define ELSE_I(name, target) else READ_I(name,target)
#define ELSE_R(name, target) else if(std::strcmp(key,name)==0) { if(!real)return false; target=static_cast<decltype(target)>(r); }
        READ_I("configured",settings.configured)
        ELSE_I("controls.buttonLayout",settings.controls.buttonLayout)
        ELSE_I("controls.movementMode",settings.controls.movementMode)
        ELSE_I("controls.controllerLookSensitivity",settings.controls.controllerLookSensitivity)
        ELSE_I("controls.controllerInvertVertical",settings.controls.controllerInvertVertical)
        ELSE_I("controls.controllerAutoLookCentering",settings.controls.controllerAutoLookCentering)
        ELSE_I("controls.controllerVibration",settings.controls.controllerVibration)
        ELSE_I("controls.controllerSwapShoulders",settings.controls.controllerSwapShoulders)
        ELSE_I("controls.controllerInvertHorizontal",settings.controls.controllerInvertHorizontal)
        ELSE_I("controls.mouseLookSensitivity",settings.controls.mouseLookSensitivity)
        ELSE_I("controls.mouseInvertVertical",settings.controls.mouseInvertVertical)
        ELSE_I("controls.mouseInvertHorizontal",settings.controls.mouseInvertHorizontal)
        ELSE_I("controls.unidentifiedToggle",settings.controls.unidentifiedToggle)
        ELSE_I("controls.mouseAimSmoothing",settings.controls.mouseAimSmoothing)
        ELSE_R("controls.adsSensitivityModifier",settings.controls.adsSensitivityModifier)
        ELSE_I("controls.doublePressDelay",settings.controls.doublePressDelay)
        ELSE_I("audio.voiceOutputMode",settings.audio.voiceOutputMode)
        ELSE_I("audio.teamVoiceChannel",settings.audio.teamVoiceChannel)
        ELSE_I("audio.reservedMode",settings.audio.reservedMode)
        ELSE_I("audio.migrationVersion",settings.audio.migrationVersion)
        ELSE_I("audio.chatVolume",settings.audio.chatVolume)
        ELSE_I("audio.muteWhenUnfocused",settings.audio.muteWhenUnfocused)
        ELSE_I("audio.soundEffectsVolume",settings.audio.soundEffectsVolume)
        ELSE_I("audio.dialogueVolume",settings.audio.dialogueVolume)
        ELSE_I("audio.musicVolume",settings.audio.musicVolume)
        ELSE_I("display.brightness",settings.display.brightness)
        ELSE_I("display.showFps",settings.display.showFps)
        ELSE_I("display.hdrMode",settings.display.hdrMode)
        ELSE_R("display.calibrationPrimary",settings.display.calibrationPrimary)
        ELSE_R("display.calibrationAlpha",settings.display.calibrationAlpha)
        ELSE_I("display.motionBlur",settings.display.motionBlur)
        ELSE_I("display.filmGrain",settings.display.filmGrain)
        ELSE_I("display.chromaticAberration",settings.display.chromaticAberration)
        ELSE_I("pc.seedVersion",settings.pc.seedVersion)
        ELSE_I("pc.voiceChatEnabled",settings.pc.voiceChatEnabled)
        ELSE_I("pc.verticalSyncMode",settings.pc.verticalSyncMode)
        ELSE_I("pc.fieldOfViewAdjustment",settings.pc.fieldOfViewAdjustment)
        ELSE_I("pc.useLocalKeyBindings",settings.pc.useLocalKeyBindings)
        ELSE_I("interface.subtitlesMode",settings.interface.subtitlesMode)
        ELSE_I("interface.colorblindMode",settings.interface.colorblindMode)
        ELSE_I("interface.helmetMode",settings.interface.helmetMode)
        ELSE_I("interface.hudOpacity",settings.interface.hudOpacity)
        ELSE_I("interface.displayHints",settings.interface.displayHints)
        ELSE_I("interface.backgroundOpacity",settings.interface.backgroundOpacity)
        ELSE_I("interface.reticleLocation",settings.interface.reticleLocation)
        ELSE_I("interface.reticleColor",settings.interface.reticleColor)
        ELSE_I("interface.textSize",settings.interface.textSize)
        ELSE_I("interface.textColor",settings.interface.textColor)
        ELSE_I("interface.textBackgroundStyle",settings.interface.textBackgroundStyle)
        ELSE_I("interface.textBackgroundOpacity",settings.interface.textBackgroundOpacity)
        ELSE_I("interface.reservedTextMode",settings.interface.reservedTextMode)
        ELSE_I("interface.subtitleOptionsEntry",settings.interface.subtitleOptionsEntry)
        ELSE_I("social.preferGoodConnection",settings.social.preferGoodConnection)
        ELSE_I("social.textChatMode",settings.social.textChatMode)
        ELSE_I("social.showRealNames",settings.social.showRealNames)
        ELSE_I("social.clanInviteNotifications",settings.social.clanInviteNotifications)
        ELSE_I("social.profanityFilter",settings.social.profanityFilter)
        ELSE_I("social.voiceChatEnabled",settings.social.voiceChatEnabled)
        ELSE_I("social.whisperChatMode",settings.social.whisperChatMode)
        ELSE_I("social.teamChatJoinMode",settings.social.teamChatJoinMode)
        ELSE_I("social.localChatJoinMode",settings.social.localChatJoinMode)
        ELSE_I("social.clanChatJoinMode",settings.social.clanChatJoinMode)
        ELSE_I("social.chatAutoHideMode",settings.social.chatAutoHideMode)
        ELSE_I("keyBindings.configured",settings.keyBindings.configured)
        else { matched=false; }
#undef READ_I
#undef ELSE_I
#undef ELSE_R
        if(!matched) return false;
        ++count;
    }
    if(result!=SQLITE_DONE||count!=64) return false;
    Statement bindings{"SELECT action,primary_input,secondary_input FROM key_bindings ORDER BY action"};
    if(!bindings.ready()) return false;
    std::size_t bindingCount=0;
    while((result=sqlite3_step(bindings.value))==SQLITE_ROW) {
        const int action=sqlite3_column_int(bindings.value,0);
        if(action<0 || static_cast<std::size_t>(action)!=bindingCount
            || bindingCount>=settings.keyBindings.values.size()) return false;
        auto& binding=settings.keyBindings.values[bindingCount];
        if(sqlite3_column_type(bindings.value,1)!=SQLITE_NULL) {
            const int value=sqlite3_column_int(bindings.value,1);
            if(value<0 || value>(std::numeric_limits<std::uint16_t>::max)()) return false;
            binding.primary=static_cast<std::uint16_t>(value);
        }
        if(sqlite3_column_type(bindings.value,2)!=SQLITE_NULL) {
            const int value=sqlite3_column_int(bindings.value,2);
            if(value<0 || value>(std::numeric_limits<std::uint16_t>::max)()) return false;
            binding.secondary=static_cast<std::uint16_t>(value);
        }
        ++bindingCount;
    }
    return result==SQLITE_DONE&&bindingCount==settings.keyBindings.values.size() && account::settings::valid(settings);
}

[[nodiscard]] account::inventory::Item* find_item(AccountState& accountState,
                                                   std::uint64_t instanceSoid) noexcept {
    for(std::size_t c=0;c<accountState.characterCount;++c) {
        auto& character=accountState.characters[c];
        for(auto& slot:character.equipment.slots) if(slot && slot->instanceSoid==instanceSoid) return &*slot;
        for(std::size_t i=0;i<character.inventory.count;++i)
            if(character.inventory.values[i].instanceSoid==instanceSoid) return &character.inventory.values[i];
    }
    return nullptr;
}

[[nodiscard]] bool read_account(AccountState& accountState) noexcept {
    accountState={};
    Statement accountRow{"SELECT primary_soid FROM account WHERE id=1"};
    if(!accountRow.ready() || sqlite3_step(accountRow.value)!=SQLITE_ROW
        || !parse_u64(sqlite3_column_text(accountRow.value,0),accountState.primarySoid)
        || sqlite3_step(accountRow.value)!=SQLITE_DONE) return false;
    Statement rewards{"SELECT position,definition_hash,quantity FROM dismantle_rewards ORDER BY position"};
    if(!rewards.ready()) return false;
    int result{};while((result=sqlite3_step(rewards.value))==SQLITE_ROW) {
        const auto position=static_cast<std::size_t>(sqlite3_column_int64(rewards.value,0));
        const auto hash=sqlite3_column_int64(rewards.value,1);
        if(position!=accountState.dismantleRewardCount || position>=accountState.dismantleRewards.size()
            || hash<0 || hash>(std::numeric_limits<std::uint32_t>::max)()) return false;
        auto& value=accountState.dismantleRewards[position];
        std::int32_t quantity{};if(!column_i32(rewards.value,2,quantity))return false;
        value.definitionHash=static_cast<std::uint32_t>(hash);value.quantity=quantity;
        ++accountState.dismantleRewardCount;
    }
    if(result!=SQLITE_DONE)return false;
    Statement profiles{"SELECT position,instance_soid,definition_hash,quantity,mutation_serial FROM profile_items ORDER BY position"};
    if(!profiles.ready()) return false;
    while((result=sqlite3_step(profiles.value))==SQLITE_ROW) {
        const auto position=static_cast<std::size_t>(sqlite3_column_int64(profiles.value,0));
        const auto hash=sqlite3_column_int64(profiles.value,2);
        if(position!=accountState.profileItemCount || position>=accountState.profileItems.size() || hash<0
            || hash>(std::numeric_limits<std::uint32_t>::max)()) return false;
        auto& p=accountState.profileItems[position];
        if(!parse_u64(sqlite3_column_text(profiles.value,1),p.instanceSoid)) return false;
        std::int32_t quantity{},serial{};if(!column_i32(profiles.value,3,quantity)||!column_i32(profiles.value,4,serial))return false;
        p.definitionHash=static_cast<std::uint32_t>(hash);p.quantity=quantity;
        p.mutationSerial=serial;++accountState.profileItemCount;
    }
    if(result!=SQLITE_DONE)return false;
    Statement characters{"SELECT * FROM characters ORDER BY position"};
    if(!characters.ready()) return false;
    while((result=sqlite3_step(characters.value))==SQLITE_ROW) {
        const auto position=static_cast<std::size_t>(sqlite3_column_int64(characters.value,0));
        if(position!=accountState.characterCount || position>=accountState.characters.size()) return false;
        auto& c=accountState.characters[position];
        if(!parse_u64(sqlite3_column_text(characters.value,1),c.soid)) return false;
        std::int32_t selected{};if(!column_i32(characters.value,2,selected)||(selected!=0&&selected!=1))return false;
        // Selection is a session handshake. The saved value is last-selection history only.
        c.selected=false;
        const int race=sqlite3_column_int(characters.value,3),gender=sqlite3_column_int(characters.value,4),characterClass=sqlite3_column_int(characters.value,5),level=sqlite3_column_int(characters.value,6);
        const int accepted=sqlite3_column_int(characters.value,7),preview=sqlite3_column_int(characters.value,8),bypass=sqlite3_column_int(characters.value,11);
        if(race<0||race>2||gender<0||gender>1||characterClass<0||characterClass>2||level<0||level>255
            ||accepted<0||accepted>1||preview<0||preview>1||bypass<0||bypass>1)return false;
        c.race=static_cast<CharacterRace>(race);c.gender=static_cast<CharacterGender>(gender);
        c.characterClass=static_cast<CharacterClass>(characterClass);c.level=static_cast<std::uint8_t>(level);
        c.accepted=accepted!=0;c.previewAvailable=preview!=0;
        if(sqlite3_column_type(characters.value,9)!=SQLITE_FLOAT
            &&sqlite3_column_type(characters.value,9)!=SQLITE_INTEGER)return false;
        c.appearanceValue=static_cast<float>(sqlite3_column_double(characters.value,9));
        if(!std::isfinite(c.appearanceValue))return false;
        const auto destination=sqlite3_column_int64(characters.value,10);
        if(destination<0 || destination>(std::numeric_limits<std::uint32_t>::max)()) return false;
        c.lastOrbitedDestination=static_cast<std::uint32_t>(destination);c.contentBypass=bypass!=0;
        std::int32_t abilities[5]{};
        for(int ability=0;ability<5;++ability)
            if(!column_i32(characters.value,12+ability,abilities[ability])
                ||abilities[ability]<0||abilities[ability]>255)return false;
        c.movementAbilityEntry=static_cast<std::uint8_t>(abilities[0]);
        c.grenadeAbilityEntry=static_cast<std::uint8_t>(abilities[1]);
        c.superAbilityEntry=static_cast<std::uint8_t>(abilities[2]);
        c.meleeAbilityEntry=static_cast<std::uint8_t>(abilities[3]);
        c.classAbilityEntry=static_cast<std::uint8_t>(abilities[4]);
        const auto serial=sqlite3_column_int64(characters.value,17);
        if(serial<0 || serial>(std::numeric_limits<std::uint32_t>::max)()) return false;
        c.nextInventorySerial=static_cast<std::uint32_t>(serial);
        std::int32_t campaigns{};
        if(!column_i32(characters.value,18,campaigns) || campaigns<0 || campaigns>7) return false;
        c.vendorCampaigns=static_cast<std::uint8_t>(campaigns);++accountState.characterCount;
    }
    if(result!=SQLITE_DONE)return false;
    Statement items{"SELECT character_soid,location,position,instance_soid,definition_hash,level,quantity,mutation_serial,flags,socket_policy,postmaster FROM character_items ORDER BY character_soid,location,position"};
    if(!items.ready()) return false;
    while((result=sqlite3_step(items.value))==SQLITE_ROW) {
        std::uint64_t characterSoid{},instanceSoid{};
        if(!parse_u64(sqlite3_column_text(items.value,0),characterSoid)
            ||!parse_u64(sqlite3_column_text(items.value,3),instanceSoid)) return false;
        CharacterState* character=nullptr;
        for(std::size_t i=0;i<accountState.characterCount;++i) if(accountState.characters[i].soid==characterSoid) character=&accountState.characters[i];
        if(character==nullptr) return false;
        const int location=sqlite3_column_int(items.value,1);const auto position=static_cast<std::size_t>(sqlite3_column_int64(items.value,2));
        account::inventory::Item* item=nullptr;
        if(location==0 && position<character->equipment.slots.size()) { character->equipment.slots[position].emplace();item=&*character->equipment.slots[position]; }
        else if(location==1 && position==character->inventory.count && position<character->inventory.values.size()) { item=&character->inventory.values[position];++character->inventory.count; }
        else return false;
        const auto hash=sqlite3_column_int64(items.value,4);const auto flags=sqlite3_column_int64(items.value,8);
        if(hash<0||hash>(std::numeric_limits<std::uint32_t>::max)()||flags<0||flags>(std::numeric_limits<std::uint32_t>::max)()) return false;
        std::int32_t itemLevel{},quantity{},serial{};const int policy=sqlite3_column_int(items.value,9);
        if(!column_i32(items.value,5,itemLevel)||!column_i32(items.value,6,quantity)||!column_i32(items.value,7,serial)||policy<0||policy>1)return false;
        std::int32_t postmaster{};
        if(!column_i32(items.value,10,postmaster) || (postmaster!=0 && postmaster!=1)) return false;
        item->postmaster=postmaster!=0;
        item->instanceSoid=instanceSoid;item->definitionHash=static_cast<std::uint32_t>(hash);
        item->level=itemLevel;item->quantity=quantity;item->mutationSerial=serial;
        item->flags=static_cast<std::uint32_t>(flags);item->sockets.policy=static_cast<account::inventory::SocketPolicy>(policy);
    }
    if(result!=SQLITE_DONE)return false;
    Statement sockets{"SELECT instance_soid,lane,plug_hash FROM item_sockets ORDER BY instance_soid,lane"};
    if(!sockets.ready()) return false;
    while((result=sqlite3_step(sockets.value))==SQLITE_ROW) {
        std::uint64_t instanceSoid{};if(!parse_u64(sqlite3_column_text(sockets.value,0),instanceSoid)) return false;
        auto* item=find_item(accountState,instanceSoid);const auto lane=static_cast<std::size_t>(sqlite3_column_int64(sockets.value,1));
        if(item==nullptr||lane!=item->sockets.plugCount||lane>=item->sockets.plugs.size()) return false;
        if(sqlite3_column_type(sockets.value,2)!=SQLITE_NULL) {
            const auto hash=sqlite3_column_int64(sockets.value,2);if(hash<0||hash>(std::numeric_limits<std::uint32_t>::max)()) return false;
            item->sockets.plugs[lane]=static_cast<std::uint32_t>(hash);
        }
        ++item->sockets.plugCount;
    }
    if (result != SQLITE_DONE) return false;
    Statement rolls{"SELECT instance_soid,entropy,lane_mask,owned_rows FROM item_rolls"};
    if (!rolls.ready()) return false;
    while ((result = sqlite3_step(rolls.value)) == SQLITE_ROW) {
        std::uint64_t instanceSoid{};
        if (!parse_u64(sqlite3_column_text(rolls.value, 0), instanceSoid)) return false;
        auto* item = find_item(accountState, instanceSoid);
        std::int32_t mask{};
        if (item == nullptr || !column_i32(rolls.value, 2, mask) || mask < 0
            || mask >= (1 << account::inventory::kPlugCapacity)
            || sqlite3_column_type(rolls.value, 1) != SQLITE_BLOB
            || sqlite3_column_bytes(rolls.value, 1) != sizeof(item->randomRoll)
            || sqlite3_column_type(rolls.value, 3) != SQLITE_BLOB
            || sqlite3_column_bytes(rolls.value, 3) != sizeof(item->availablePlugRows)) return false;
        std::memcpy(item->randomRoll.data(), sqlite3_column_blob(rolls.value, 1), sizeof(item->randomRoll));
        std::memcpy(item->availablePlugRows.data(), sqlite3_column_blob(rolls.value, 3), sizeof(item->availablePlugRows));
        item->rolledLaneMask = static_cast<std::uint16_t>(mask);
    }
    return result==SQLITE_DONE&&read_vendor_state(accountState)&&read_settings(accountState.settings) && account::valid(accountState);
}

[[nodiscard]] bool load_metadata() noexcept {
    Statement rows{"SELECT key,value FROM metadata ORDER BY key"};
    if(!rows.ready()||sqlite3_step(rows.value)!=SQLITE_ROW) return false;
    const char* first=reinterpret_cast<const char*>(sqlite3_column_text(rows.value,0));
    if(first==nullptr||std::strcmp(first,"account_revision")!=0)return false;
    accountRevision=sqlite3_column_int64(rows.value,1);
    if(accountRevision<0||sqlite3_step(rows.value)!=SQLITE_ROW)return false;
    const char* second=reinterpret_cast<const char*>(sqlite3_column_text(rows.value,0));
    if(second==nullptr||std::strcmp(second,"legacy_import_complete")!=0
        ||sqlite3_column_int64(rows.value,1)!=1||sqlite3_step(rows.value)!=SQLITE_ROW)return false;
    const char* third=reinterpret_cast<const char*>(sqlite3_column_text(rows.value,0));
    const auto epoch=sqlite3_column_int64(rows.value,1);
    if(third==nullptr||std::strcmp(third,"reward_epoch")!=0||epoch<=0
        ||sqlite3_step(rows.value)!=SQLITE_DONE)return false;
    rewardEpoch=static_cast<std::uint64_t>(epoch);
    Statement allocator{"SELECT name,next_value FROM allocators ORDER BY name"};
    if(!allocator.ready()) return false;
    bool item=false,profile=false;int result{};
    while((result=sqlite3_step(allocator.value))==SQLITE_ROW) {
        const char* name=reinterpret_cast<const char*>(sqlite3_column_text(allocator.value,0));std::uint64_t value{};
        if(name==nullptr||!parse_u64(sqlite3_column_text(allocator.value,1),value)) return false;
        if(std::strcmp(name,"item_instance")==0){nextItemSoid=value;item=true;}
        else if(std::strcmp(name,"profile_item_instance")==0){nextProfileItemSoid=value;profile=true;}
        else return false;
    }
    return result==SQLITE_DONE&&item&&profile&&nextItemSoid>=kFirstGeneratedItemSoid&&nextProfileItemSoid>=kFirstProfileItemSoid;
}

[[nodiscard]] bool advance_reward_epoch() noexcept {
    if(rewardEpoch>=static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))return false;
    Statement update{"UPDATE metadata SET value=value+1 WHERE key='reward_epoch' AND value=?1"};
    if(!update.ready()||!bind_i64(update.value,1,static_cast<std::int64_t>(rewardEpoch))
        ||!step_done(update.value)||sqlite3_changes(database)!=1)return false;
    ++rewardEpoch;return true;
}

[[nodiscard]] bool insert_flag(Scope scope,std::uint64_t owner,std::uint32_t slot,std::uint8_t value) noexcept {
    if(value==0)return true;
    Statement s{"INSERT INTO durable_flags VALUES(?1,?2,?3,?4)"};
    return s.ready()&&sqlite3_bind_int(s.value,1,scope_value(scope))==SQLITE_OK&&bind_u64(s.value,2,owner)
        &&bind_i64(s.value,3,slot)&&sqlite3_bind_int(s.value,4,value)==SQLITE_OK&&step_done(s.value);
}
[[nodiscard]] bool insert_objective(Scope scope,std::uint64_t owner,std::uint32_t slot,std::int32_t value) noexcept {
    if(value==0)return true;
    Statement s{"INSERT INTO durable_objectives VALUES(?1,?2,?3,?4)"};
    return s.ready()&&sqlite3_bind_int(s.value,1,scope_value(scope))==SQLITE_OK&&bind_u64(s.value,2,owner)
        &&bind_i64(s.value,3,slot)&&sqlite3_bind_int(s.value,4,value)==SQLITE_OK&&step_done(s.value);
}
[[nodiscard]] bool insert_progression(Scope scope,std::uint64_t owner,std::uint32_t definition,std::uint8_t lane,std::int32_t value) noexcept {
    if(value==0)return true;
    Statement s{"INSERT INTO durable_progressions VALUES(?1,?2,?3,?4,?5)"};
    return s.ready()&&sqlite3_bind_int(s.value,1,scope_value(scope))==SQLITE_OK&&bind_u64(s.value,2,owner)
        &&bind_i64(s.value,3,definition)&&sqlite3_bind_int(s.value,4,lane)==SQLITE_OK
        &&sqlite3_bind_int(s.value,5,value)==SQLITE_OK&&step_done(s.value);
}

[[nodiscard]] bool write_unlocks(const unlocks::ScopedTable& table,std::uint64_t accountSoid) noexcept {
    for(std::size_t i=0;i<table.accountFlags.size();++i) if(!insert_flag(Scope::account,accountSoid,static_cast<std::uint32_t>(i),table.accountFlags[i]))return false;
    for(std::size_t i=0;i<table.profileFlags.size();++i) if(!insert_flag(Scope::profile,accountSoid,static_cast<std::uint32_t>(i),table.profileFlags[i]))return false;
    for(std::size_t i=0;i<table.objectiveValues.size();++i) if(!insert_objective(Scope::account,accountSoid,static_cast<std::uint32_t>(i),table.objectiveValues[i]))return false;
    for(std::size_t d=0;d<table.accountProgressions.size();++d)for(std::size_t lane=0;lane<table.accountProgressions[d].size();++lane)
        if(!insert_progression(Scope::account,accountSoid,static_cast<std::uint32_t>(d),static_cast<std::uint8_t>(lane),table.accountProgressions[d][lane]))return false;
    for(std::size_t c=0;c<table.characterCount;++c) {
        const auto& character=table.characters[c];if(character.characterSoid==0)return false;
        for(std::size_t i=0;i<character.flags.size();++i)if(!insert_flag(Scope::character,character.characterSoid,static_cast<std::uint32_t>(i),character.flags[i]))return false;
        for(std::size_t i=0;i<character.objectFlags.size();++i)if(!insert_flag(Scope::characterObject,character.characterSoid,static_cast<std::uint32_t>(i),character.objectFlags[i]))return false;
        for(std::size_t i=0;i<character.objectValues.size();++i)if(!insert_objective(Scope::characterObject,character.characterSoid,static_cast<std::uint32_t>(i),character.objectValues[i]))return false;
        for(std::size_t d=0;d<character.progressions.size();++d)for(std::size_t lane=0;lane<character.progressions[d].size();++lane)
            if(!insert_progression(Scope::character,character.characterSoid,static_cast<std::uint32_t>(d),static_cast<std::uint8_t>(lane),character.progressions[d][lane]))return false;
    }
    return true;
}

[[nodiscard]] unlocks::CharacterTable* character_table(unlocks::ScopedTable& table,std::uint64_t soid) noexcept {
    for(std::size_t i=0;i<table.characterCount;++i)if(table.characters[i].characterSoid==soid)return &table.characters[i];
    return nullptr;
}

[[nodiscard]] bool read_unlocks(const AccountState& accountState,unlocks::ScopedTable& table) noexcept {
    table={};table.characterCount=accountState.characterCount;
    for(std::size_t i=0;i<table.characterCount;++i)table.characters[i].characterSoid=accountState.characters[i].soid;
    Statement flags{"SELECT scope,owner_soid,slot,value FROM durable_flags ORDER BY scope,owner_soid,slot"};
    if(!flags.ready())return false;int result{};
    while((result=sqlite3_step(flags.value))==SQLITE_ROW) {
        std::int32_t scope{},value{};std::uint64_t owner{};const auto slot=sqlite3_column_int64(flags.value,2);
        if(!column_i32(flags.value,0,scope)||!column_i32(flags.value,3,value)
            ||!parse_u64(sqlite3_column_text(flags.value,1),owner)||slot<0||value<0||value>255)return false;
        if(scope==scope_value(Scope::account)&&owner==accountState.primarySoid&&static_cast<std::size_t>(slot)<table.accountFlags.size())table.accountFlags[slot]=static_cast<std::uint8_t>(value);
        else if(scope==scope_value(Scope::profile)&&owner==accountState.primarySoid&&static_cast<std::size_t>(slot)<table.profileFlags.size())table.profileFlags[slot]=static_cast<std::uint8_t>(value);
        else if(scope==scope_value(Scope::character)) { auto* c=character_table(table,owner);if(c==nullptr||static_cast<std::size_t>(slot)>=c->flags.size())return false;c->flags[slot]=static_cast<std::uint8_t>(value); }
        else if(scope==scope_value(Scope::characterObject)) { auto* c=character_table(table,owner);if(c==nullptr||static_cast<std::size_t>(slot)>=c->objectFlags.size())return false;c->objectFlags[slot]=static_cast<std::uint8_t>(value); }
        else return false;
    }
    if(result!=SQLITE_DONE)return false;
    Statement objectives{"SELECT scope,owner_soid,slot,value FROM durable_objectives ORDER BY scope,owner_soid,slot"};
    if(!objectives.ready())return false;
    while((result=sqlite3_step(objectives.value))==SQLITE_ROW) {
        std::int32_t scope{},value{};std::uint64_t owner{};const auto slot=sqlite3_column_int64(objectives.value,2);
        if(!column_i32(objectives.value,0,scope)||!column_i32(objectives.value,3,value)
            ||!parse_u64(sqlite3_column_text(objectives.value,1),owner)||slot<0)return false;
        if(scope==scope_value(Scope::account)&&owner==accountState.primarySoid&&static_cast<std::size_t>(slot)<table.objectiveValues.size())table.objectiveValues[slot]=value;
        else if(scope==scope_value(Scope::characterObject)){auto* c=character_table(table,owner);if(c==nullptr||static_cast<std::size_t>(slot)>=c->objectValues.size())return false;c->objectValues[slot]=value;}
        else return false;
    }
    if(result!=SQLITE_DONE)return false;
    Statement progressions{"SELECT scope,owner_soid,definition_index,lane,value FROM durable_progressions ORDER BY scope,owner_soid,definition_index,lane"};
    if(!progressions.ready())return false;
    while((result=sqlite3_step(progressions.value))==SQLITE_ROW) {
        std::int32_t scope{},value{};std::uint64_t owner{};const auto definition=sqlite3_column_int64(progressions.value,2);const auto lane=sqlite3_column_int64(progressions.value,3);
        if(!column_i32(progressions.value,0,scope)||!column_i32(progressions.value,4,value)
            ||!parse_u64(sqlite3_column_text(progressions.value,1),owner)||definition<0||lane<0||lane>=static_cast<std::int64_t>(unlocks::kProgressionLaneCount))return false;
        if(scope==scope_value(Scope::account)&&owner==accountState.primarySoid&&static_cast<std::size_t>(definition)<table.accountProgressions.size())table.accountProgressions[definition][lane]=value;
        else if(scope==scope_value(Scope::character)){auto* c=character_table(table,owner);if(c==nullptr||static_cast<std::size_t>(definition)>=c->progressions.size())return false;c->progressions[definition][lane]=value;}
        else return false;
    }
    return result==SQLITE_DONE;
}

[[nodiscard]] bool write_family5(const Family5State& family) noexcept {
    Statement flag{"INSERT INTO family5_flags VALUES(?1,?2)"};Statement value{"INSERT INTO family5_values VALUES(?1,?2)"};
    if(!flag.ready()||!value.ready()||family.flagCount>family.flags.size()||family.valueCount>family.values.size())return false;
    for(std::size_t i=0;i<family.flagCount;++i){sqlite3_reset(flag.value);sqlite3_clear_bindings(flag.value);if(sqlite3_bind_int(flag.value,1,family.flags[i].slot)!=SQLITE_OK||sqlite3_bind_int(flag.value,2,family.flags[i].value)!=SQLITE_OK||!step_done(flag.value))return false;}
    for(std::size_t i=0;i<family.valueCount;++i){sqlite3_reset(value.value);sqlite3_clear_bindings(value.value);if(sqlite3_bind_int(value.value,1,family.values[i].slot)!=SQLITE_OK||sqlite3_bind_int(value.value,2,family.values[i].value)!=SQLITE_OK||!step_done(value.value))return false;}
    return true;
}
[[nodiscard]] bool read_family5(Family5State& family) noexcept {
    family={};Statement flags{"SELECT slot,value FROM family5_flags ORDER BY slot"};Statement values{"SELECT slot,value FROM family5_values ORDER BY slot"};
    if(!flags.ready()||!values.ready())return false;int result{};
    while((result=sqlite3_step(flags.value))==SQLITE_ROW){if(family.flagCount>=family.flags.size())return false;std::int32_t slot{},value{};if(!column_i32(flags.value,0,slot)||!column_i32(flags.value,1,value)||slot<0||slot>(std::numeric_limits<std::uint16_t>::max)()||value<0||value>255)return false;family.flags[family.flagCount++]={static_cast<std::uint16_t>(slot),static_cast<std::uint8_t>(value)};}
    if(result!=SQLITE_DONE)return false;
    while((result=sqlite3_step(values.value))==SQLITE_ROW){if(family.valueCount>=family.values.size())return false;std::int32_t slot{},value{};if(!column_i32(values.value,0,slot)||!column_i32(values.value,1,value)||slot<0||slot>(std::numeric_limits<std::uint16_t>::max)())return false;family.values[family.valueCount++]={static_cast<std::uint16_t>(slot),value};}
    return result==SQLITE_DONE;
}

[[nodiscard]] bool advance_revision() noexcept {
    Statement update{"UPDATE metadata SET value=value+1 WHERE key='account_revision' AND value=?1"};
    if(!update.ready()||!bind_i64(update.value,1,accountRevision)||!step_done(update.value)
        ||sqlite3_changes(database)!=1) return false;
    ++accountRevision;return true;
}

[[nodiscard]] bool update_column(const char* table,const char* column,const char* from,
                                 const char* to) noexcept {
    std::array<char,160> sql{};
    const int count=std::snprintf(sql.data(),sql.size(),"UPDATE %s SET %s=?1 WHERE %s=?2",table,column,column);
    if(count<=0||static_cast<std::size_t>(count)>=sql.size())return false;
    Statement statement{sql.data()};
    return statement.ready()&&sqlite3_bind_text(statement.value,1,to,-1,SQLITE_TRANSIENT)==SQLITE_OK
        &&sqlite3_bind_text(statement.value,2,from,-1,SQLITE_TRANSIENT)==SQLITE_OK&&step_done(statement.value);
}
[[nodiscard]] bool delete_owner_rows(const char* table,const char* column,const char* owner) noexcept {
    std::array<char,160> sql{};
    const int count=std::snprintf(sql.data(),sql.size(),"DELETE FROM %s WHERE %s=?1",table,column);
    if(count<=0||static_cast<std::size_t>(count)>=sql.size())return false;
    Statement statement{sql.data()};
    return statement.ready()&&sqlite3_bind_text(statement.value,1,owner,-1,SQLITE_TRANSIENT)==SQLITE_OK&&step_done(statement.value);
}
[[nodiscard]] bool migrate_owners(const AccountState& before,const AccountState& after) noexcept {
    // Only the owners `before` already holds are renamed. A character added by `after` has no rows
    // to move yet, so the count may grow; it may not shrink, which would strand its rows.
    if(after.characterCount<before.characterCount)return false;
    struct Mapping { std::array<char,17> before{},after{};std::array<char,4> temporary{};bool changed{}; };
    std::array<Mapping,kCharacterCapacity+1> mappings{};const std::size_t count=before.characterCount+1U;
    for(std::size_t i=0;i<count;++i) {
        const std::uint64_t oldValue=i==0?before.primarySoid:before.characters[i-1U].soid;
        const std::uint64_t newValue=i==0?after.primarySoid:after.characters[i-1U].soid;
        if(!format_u64(oldValue,mappings[i].before)||!format_u64(newValue,mappings[i].after))return false;
        mappings[i].temporary[0]='~';mappings[i].temporary[1]=static_cast<char>('0'+i);
        mappings[i].changed=oldValue!=newValue;
    }
    // Evacuate every changing owner first. The non-hex temporary keys cannot collide with a
    // persisted SOID, so swaps and overlapping account/character rebases remain lossless.
    for(std::size_t i=0;i<count;++i)if(mappings[i].changed) {
        for(const char* table:{"durable_flags","durable_objectives","durable_progressions"})
            if(!update_column(table,"owner_soid",mappings[i].before.data(),mappings[i].temporary.data()))return false;
        if(i==0) {
            if(!update_column("reward_debts","account_soid",mappings[i].before.data(),mappings[i].temporary.data()))return false;
        } else if(!update_column("missions","character_soid",mappings[i].before.data(),mappings[i].temporary.data())
                  ||!update_column("reward_debts","character_soid",mappings[i].before.data(),mappings[i].temporary.data()))return false;
    }
    for(std::size_t i=0;i<count;++i)if(mappings[i].changed) {
        for(const char* table:{"durable_flags","durable_objectives","durable_progressions"})
            if(!update_column(table,"owner_soid",mappings[i].temporary.data(),mappings[i].after.data()))return false;
        if(i==0) {
            if(!update_column("reward_debts","account_soid",mappings[i].temporary.data(),mappings[i].after.data()))return false;
        } else if(!update_column("missions","character_soid",mappings[i].temporary.data(),mappings[i].after.data())
                  ||!update_column("reward_debts","character_soid",mappings[i].temporary.data(),mappings[i].after.data()))return false;
    }
    return true;
}

/**
 * Puts the characters of a loaded account on the account key plus one plus their position, which is
 * what sign-in rebases them to. Deleting a character leaves the later ones under their old keys for
 * the rest of the session, so a save can hold a gap. The rows they own are renamed with them. An
 * account that would not validate under those keys is left as it is.
 */
[[nodiscard]] bool canonicalize_character_keys(AccountState& account) noexcept {
    bool canonical=true;
    for(std::size_t i=0;i<account.characterCount;++i)
        canonical=canonical&&account.characters[i].soid==account.primarySoid+1U+i;
    if(canonical) return true;
    static AccountState before{};
    before=account;
    for(std::size_t i=0;i<account.characterCount;++i) account.characters[i].soid=account.primarySoid+1U+i;
    if(!account::valid(account)) {account=before;return true;}
    return migrate_owners(before,account)&&write_account(account);
}

[[nodiscard]] bool write_mission(const MissionRecord& record) noexcept {
    if(record.characterSoid==0||record.missionHash==0||record.updatedUtc<0) return false;
    Statement statement{"INSERT INTO missions VALUES(?1,?2,?3,?4,?5,?6,?7,?8) ON CONFLICT(character_soid,mission_hash) DO UPDATE SET checkpoint_hash=excluded.checkpoint_hash,checkpoint_slice_set=excluded.checkpoint_slice_set,activity_index=excluded.activity_index,progress=excluded.progress,completed=CASE WHEN missions.completed<>0 OR excluded.completed<>0 THEN 1 ELSE 0 END,updated_utc=excluded.updated_utc"};
    return statement.ready()&&bind_u64(statement.value,1,record.characterSoid)
        &&bind_i64(statement.value,2,record.missionHash)&&bind_i64(statement.value,3,record.checkpointHash)
        &&sqlite3_bind_int(statement.value,4,record.checkpointSliceSet)==SQLITE_OK
        &&sqlite3_bind_int(statement.value,5,record.activityIndex)==SQLITE_OK
        &&sqlite3_bind_int(statement.value,6,record.progress)==SQLITE_OK
        &&sqlite3_bind_int(statement.value,7,record.completed)==SQLITE_OK&&bind_i64(statement.value,8,record.updatedUtc)&&step_done(statement.value);
}

} // namespace

bool initialize(void* module,const AccountState& legacyAccount,const unlocks::Table& legacyUnlocks,
                const Family5State& legacyFamily5,AccountState& loadedAccount,
                unlocks::ScopedTable& loadedUnlocks,Family5State& loadedFamily5) noexcept {
    Lock lock;
    if(database!=nullptr||memoryOnly) return false;
    if(module==nullptr) {
        memoryOnly=true;loadedAccount=legacyAccount;loadedUnlocks=unlocks::expand(legacyUnlocks,legacyAccount);
        loadedFamily5=legacyFamily5;return account::valid(loadedAccount);
    }
    core::path::Buffer path;
    if(!core::path::artifact_directory(module,path)||!core::path::append(path,kDatabaseName)) return false;
    const DWORD attributes=GetFileAttributesW(path.chars.data());
    const bool existed=attributes!=INVALID_FILE_ATTRIBUTES;
    if(!existed&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
    if(existed&&(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0) return false;
    if(!existed) {
        const HANDLE reserved=CreateFileW(path.chars.data(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(reserved==INVALID_HANDLE_VALUE)return false;
        CloseHandle(reserved);
    }
    if(sqlite3_open16(path.chars.data(),&database)!=SQLITE_OK) {
        log_failure("open");sqlite3_close_v2(database);database=nullptr;
        if(!existed)delete_database_files(path);return false;
    }
    sqlite3_extended_result_codes(database,1);sqlite3_busy_timeout(database,2500);int schema=-1;
    {
        Statement version{"PRAGMA user_version"};
        if(version.ready()&&sqlite3_step(version.value)==SQLITE_ROW) schema=sqlite3_column_int(version.value,0);
    }
    const bool newDatabase=!existed;
    if((newDatabase&&schema!=0)||(!newDatabase&&schema==0)||schema>kSchemaVersion||schema<0) {
        log_failure("schema_version");sqlite3_close_v2(database);database=nullptr;return false;
    }
    if(!execute("PRAGMA foreign_keys=ON;PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;PRAGMA trusted_schema=OFF")) {
        log_failure("configure");sqlite3_close_v2(database);database=nullptr;
        if(newDatabase)delete_database_files(path);return false;
    }
    if(newDatabase) {
        nextItemSoid=kFirstGeneratedItemSoid;nextProfileItemSoid=kFirstProfileItemSoid;
        accountRevision=0;rewardEpoch=1;
        loadedUnlocks=unlocks::expand(legacyUnlocks,legacyAccount);loadedFamily5=legacyFamily5;
        if(!begin()||!create_schema()||!write_account(legacyAccount)
            ||!write_unlocks(loadedUnlocks,legacyAccount.primarySoid)||!write_family5(legacyFamily5)
            ||!execute("UPDATE metadata SET value=1 WHERE key='legacy_import_complete'")||!commit()) {
            rollback();log_failure("legacy_import");sqlite3_close_v2(database);database=nullptr;delete_database_files(path);return false;
        }
        loadedAccount=legacyAccount;
    } else {
        if(!begin()||!migrate_schema(schema)||!load_metadata()||!read_account(loadedAccount)
           ||!canonicalize_character_keys(loadedAccount)
           ||!read_unlocks(loadedAccount,loadedUnlocks)||!read_family5(loadedFamily5)
           ||!advance_reward_epoch()||!commit()) {
            rollback();log_failure("load");sqlite3_close_v2(database);database=nullptr;return false;
        }
    }
    loadedUnlocks.accountSoid=loadedAccount.primarySoid;
    observe_allocators(loadedAccount);
    return true;
}

void shutdown() noexcept {
    Lock lock;(void)sqlite3_close_v2(database);database=nullptr;memoryOnly=false;accountRevision=0;rewardEpoch=0;
    nextItemSoid=kFirstGeneratedItemSoid;nextProfileItemSoid=kFirstProfileItemSoid;
}

bool enabled() noexcept { Lock lock;return database!=nullptr; }

bool backup_for_editor() noexcept {
    Lock lock;
    if (memoryOnly) return true;
    if (database == nullptr) return false;
    SYSTEMTIME now{}; GetSystemTime(&now);
    wchar_t name[160]{};
    (void)swprintf_s(name, L"editor-backups\\player-%04u%02u%02u-%02u%02u%02u-%03u-%lld.db",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        static_cast<long long>(accountRevision));
    core::path::Buffer target{};
    if (!core::path::artifact_file(name, target)) return false;
    sqlite3* backup{};
    if (sqlite3_open16(target.chars.data(), &backup) != SQLITE_OK) {
        if (backup) (void)sqlite3_close(backup);
        return false;
    }
    sqlite3_backup* job = sqlite3_backup_init(backup, "main", database, "main");
    const int copied = job ? sqlite3_backup_step(job, -1) : SQLITE_ERROR;
    const int finished = job ? sqlite3_backup_finish(job) : SQLITE_ERROR;
    const int closed = sqlite3_close(backup);
    return copied == SQLITE_DONE && finished == SQLITE_OK && closed == SQLITE_OK;
}

bool commit_account(const AccountState& before,const AccountState& after) noexcept {
    Lock lock;
    if(!account::valid(before)||!account::valid(after)) return false;
    if(memoryOnly) return true;
    if(database==nullptr||!begin()) return false;
    const std::int64_t previousRevision=accountRevision;
    if(!migrate_owners(before,after)||!write_account(after)||!advance_revision()||!commit()) {
        rollback();accountRevision=previousRevision;log_failure("commit_account");return false;
    }
    return true;
}

bool commit_character_removal(const AccountState& before,
                              const AccountState& after,
                              std::uint64_t removedSoid) noexcept {
    Lock lock;
    if(!account::valid(before)||!account::valid(after)||removedSoid==0
       ||after.primarySoid!=before.primarySoid||after.characterCount+1U!=before.characterCount) return false;
    // Everything but the removed character must carry over untouched, so no owner is renamed.
    std::size_t kept=0;bool found=false;
    for(std::size_t i=0;i<before.characterCount;++i) {
        if(before.characters[i].soid==removedSoid){found=true;continue;}
        if(kept>=after.characterCount||after.characters[kept].soid!=before.characters[i].soid)return false;
        ++kept;
    }
    if(!found||kept!=after.characterCount) return false;
    if(memoryOnly) return true;
    if(database==nullptr||!begin()) return false;
    const std::int64_t previousRevision=accountRevision;
    std::array<char,17> owner{};
    bool purged=format_u64(removedSoid,owner);
    // An owner row that outlived its character would be inherited by the next one created under the
    // same key, and a mission row would also break the character foreign key at commit.
    for(const char* table:{"durable_flags","durable_objectives","durable_progressions"})
        purged=purged&&delete_owner_rows(table,"owner_soid",owner.data());
    purged=purged&&delete_owner_rows("missions","character_soid",owner.data())
        &&delete_owner_rows("reward_debts","character_soid",owner.data());
    if(!purged||!write_account(after)||!advance_revision()||!commit()) {
        rollback();accountRevision=previousRevision;log_failure("commit_character_removal");return false;
    }
    return true;
}

bool commit_settings(const account::settings::AccountSettings& settings) noexcept {
    Lock lock;
    if (!account::settings::valid(settings)) return false;
    if (memoryOnly) return true;
    if (database == nullptr || !begin()) return false;
    const auto previousRevision = accountRevision;
    if (!write_settings(settings) || !advance_revision() || !commit()) {
        log_failure("commit_settings");
        rollback(); accountRevision = previousRevision;
        return false;
    }
    return true;
}

namespace {
[[nodiscard]] bool owns_soid(const AccountState& accountState,std::uint64_t value) noexcept {
    if(value==0||value==accountState.primarySoid) return true;
    for(std::size_t i=0;i<accountState.profileItemCount;++i) if(accountState.profileItems[i].instanceSoid==value) return true;
    for(std::size_t c=0;c<accountState.characterCount;++c) {
        const auto& character=accountState.characters[c];if(character.soid==value) return true;
        for(const auto& slot:character.equipment.slots) if(slot&&slot->instanceSoid==value) return true;
        for(std::size_t i=0;i<character.inventory.count;++i) if(character.inventory.values[i].instanceSoid==value) return true;
    }
    return false;
}
}

bool next_item_instance_soid(const AccountState& accountState,std::uint64_t& output) noexcept {
    Lock lock;std::uint64_t value=nextItemSoid;
    while(owns_soid(accountState,value)) { if(value==(std::numeric_limits<std::uint64_t>::max)()) return false;++value; }
    output=value;return value!=0;
}
bool next_profile_item_instance_soid(const AccountState& accountState,std::uint64_t& output) noexcept {
    Lock lock;std::uint64_t value=nextProfileItemSoid;
    while(owns_soid(accountState,value)) { if(value==(std::numeric_limits<std::uint64_t>::max)()) return false;++value; }
    output=value;return value!=0;
}

namespace {
[[nodiscard]] bool owner_exists(Scope scope,std::uint64_t owner) noexcept {
    const char* sql=(scope==Scope::account||scope==Scope::profile)
        ? "SELECT 1 FROM account WHERE id=1 AND primary_soid=?1"
        : "SELECT 1 FROM characters WHERE soid=?1";
    Statement statement{sql};
    return statement.ready()&&bind_u64(statement.value,1,owner)
        &&sqlite3_step(statement.value)==SQLITE_ROW&&sqlite3_step(statement.value)==SQLITE_DONE;
}
[[nodiscard]] bool load_sparse(const char* sql,Scope scope,std::uint64_t owner,std::uint32_t first,
                               std::uint32_t second,bool& found,std::int32_t& value) noexcept {
    found=false;value=0;if(memoryOnly)return true;if(database==nullptr||!owner_exists(scope,owner))return false;Statement s{sql};
    if(!s.ready()||sqlite3_bind_int(s.value,1,scope_value(scope))!=SQLITE_OK||!bind_u64(s.value,2,owner)
        ||!bind_i64(s.value,3,first)||(second!=UINT32_MAX&&!bind_i64(s.value,4,second))) return false;
    const int result=sqlite3_step(s.value);if(result==SQLITE_DONE)return true;if(result!=SQLITE_ROW)return false;
    if(!column_i32(s.value,0,value))return false;found=true;return sqlite3_step(s.value)==SQLITE_DONE;
}
[[nodiscard]] bool store_sparse(const char* sql,Scope scope,std::uint64_t owner,std::uint32_t first,
                                std::uint32_t second,std::int32_t value) noexcept {
    if(memoryOnly)return true;if(database==nullptr||!owner_exists(scope,owner)||!begin())return false;
    const auto previous=accountRevision;Statement s{sql};
    const bool written=s.ready()&&sqlite3_bind_int(s.value,1,scope_value(scope))==SQLITE_OK&&bind_u64(s.value,2,owner)
        &&bind_i64(s.value,3,first)&&(second==UINT32_MAX||bind_i64(s.value,4,second))
        &&sqlite3_bind_int(s.value,second==UINT32_MAX?4:5,value)==SQLITE_OK&&step_done(s.value);
    if(!written||!advance_revision()||!commit()){rollback();accountRevision=previous;return false;}
    return true;
}
}

bool load_flag(Scope scope,std::uint64_t owner,std::uint32_t slot,bool& found,std::uint8_t& value) noexcept {
    Lock lock;std::int32_t loaded{};if(!load_sparse("SELECT value FROM durable_flags WHERE scope=?1 AND owner_soid=?2 AND slot=?3",scope,owner,slot,UINT32_MAX,found,loaded)||loaded<0||loaded>255)return false;value=static_cast<std::uint8_t>(loaded);return true;
}
bool store_flag(Scope scope,std::uint64_t owner,std::uint32_t slot,std::uint8_t value) noexcept { Lock lock;return store_sparse("INSERT INTO durable_flags VALUES(?1,?2,?3,?4) ON CONFLICT DO UPDATE SET value=excluded.value",scope,owner,slot,UINT32_MAX,value); }
bool load_objective(Scope scope,std::uint64_t owner,std::uint32_t slot,bool& found,std::int32_t& value) noexcept { Lock lock;return load_sparse("SELECT value FROM durable_objectives WHERE scope=?1 AND owner_soid=?2 AND slot=?3",scope,owner,slot,UINT32_MAX,found,value); }
bool store_objective(Scope scope,std::uint64_t owner,std::uint32_t slot,std::int32_t value) noexcept { Lock lock;return store_sparse("INSERT INTO durable_objectives VALUES(?1,?2,?3,?4) ON CONFLICT DO UPDATE SET value=excluded.value",scope,owner,slot,UINT32_MAX,value); }
bool load_progression(Scope scope,std::uint64_t owner,std::uint32_t definition,std::uint8_t lane,bool& found,std::int32_t& value) noexcept { Lock lock;return load_sparse("SELECT value FROM durable_progressions WHERE scope=?1 AND owner_soid=?2 AND definition_index=?3 AND lane=?4",scope,owner,definition,lane,found,value); }
bool store_progression(Scope scope,std::uint64_t owner,std::uint32_t definition,std::uint8_t lane,std::int32_t value) noexcept { Lock lock;return store_sparse("INSERT INTO durable_progressions VALUES(?1,?2,?3,?4,?5) ON CONFLICT DO UPDATE SET value=excluded.value",scope,owner,definition,lane,value); }

bool load_mission(std::uint64_t characterSoid,std::uint32_t missionHash,bool& found,MissionRecord& record) noexcept {
    Lock lock;found=false;record={};if(memoryOnly)return true;if(database==nullptr)return false;
    if(!owner_exists(Scope::character,characterSoid))return false;
    Statement s{"SELECT checkpoint_hash,checkpoint_slice_set,activity_index,progress,completed,updated_utc FROM missions WHERE character_soid=?1 AND mission_hash=?2"};
    if(!s.ready()||!bind_u64(s.value,1,characterSoid)||!bind_i64(s.value,2,missionHash))return false;
    const int result=sqlite3_step(s.value);if(result==SQLITE_DONE)return true;if(result!=SQLITE_ROW)return false;
    const auto checkpoint=sqlite3_column_int64(s.value,0);if(checkpoint<0||checkpoint>(std::numeric_limits<std::uint32_t>::max)())return false;
    record.characterSoid=characterSoid;record.missionHash=missionHash;record.checkpointHash=static_cast<std::uint32_t>(checkpoint);
    std::int32_t completed{};
    if(!column_i32(s.value,1,record.checkpointSliceSet)||!column_i32(s.value,2,record.activityIndex)
        ||!column_i32(s.value,3,record.progress)||!column_i32(s.value,4,completed)
        ||(completed!=0&&completed!=1))return false;
    record.completed=completed!=0;record.updatedUtc=sqlite3_column_int64(s.value,5);
    if(record.updatedUtc<0)return false;found=true;return sqlite3_step(s.value)==SQLITE_DONE;
}
bool store_mission(const MissionRecord& record) noexcept {
    Lock lock;if(memoryOnly)return true;if(database==nullptr||!begin())return false;const auto previous=accountRevision;
    if(!write_mission(record)||!advance_revision()||!commit()){rollback();accountRevision=previous;return false;}return true;
}
bool commit_account_and_mission(const AccountState& before,const AccountState& after,const MissionRecord& record) noexcept {
    Lock lock;if(!account::valid(before)||!account::valid(after))return false;if(memoryOnly)return true;if(database==nullptr||!begin())return false;
    const auto previous=accountRevision;if(!migrate_owners(before,after)||!write_account(after)||!write_mission(record)||!advance_revision()||!commit()){rollback();accountRevision=previous;return false;}return true;
}

namespace {
[[nodiscard]] bool read_reward(sqlite3_stmt* statement,RewardDebt& debt) noexcept {
    const auto debtId=sqlite3_column_int64(statement,0);
    std::uint64_t accountSoid{},characterSoid{},runtime{},sessionId{},runId{};
    const auto mission=sqlite3_column_int64(statement,6);
    const auto definition=sqlite3_column_int64(statement,7);
    std::int32_t quantity{},credited{};
    if(debtId<=0||!parse_u64(sqlite3_column_text(statement,1),accountSoid)
        ||!parse_u64(sqlite3_column_text(statement,2),characterSoid)
        ||!parse_u64(sqlite3_column_text(statement,3),runtime)
        ||!parse_u64(sqlite3_column_text(statement,4),sessionId)
        ||!parse_u64(sqlite3_column_text(statement,5),runId)
        ||mission<=0||mission>(std::numeric_limits<std::uint32_t>::max)()
        ||definition<=0||definition>(std::numeric_limits<std::uint32_t>::max)()
        ||!column_i32(statement,8,quantity)||quantity<=0
        ||!column_i32(statement,9,credited)||credited<0||credited>quantity) return false;
    const int delivered=sqlite3_column_int(statement,10);
    if(delivered!=0&&delivered!=1)return false;
    debt={static_cast<std::uint64_t>(debtId),accountSoid,characterSoid,runtime,sessionId,runId,
          static_cast<std::uint32_t>(mission),static_cast<std::uint32_t>(definition),
          quantity,credited,delivered!=0};
    return true;
}

[[nodiscard]] bool select_reward_by_key(std::uint64_t accountSoid,
                                        std::uint64_t sessionId,
                                        std::uint64_t runId,
                                        std::uint32_t definitionHash,
                                        RewardDebt& debt) noexcept {
    Statement row{"SELECT debt_id,account_soid,character_soid,runtime_epoch,session_id,run_id,mission_hash,definition_hash,quantity,credited,delivered FROM reward_debts WHERE account_soid=?1 AND runtime_epoch=?2 AND session_id=?3 AND run_id=?4 AND definition_hash=?5"};
    if(!row.ready()||!bind_u64(row.value,1,accountSoid)||!bind_u64(row.value,2,rewardEpoch)
        ||!bind_u64(row.value,3,sessionId)||!bind_u64(row.value,4,runId)
        ||!bind_i64(row.value,5,definitionHash)
        ||sqlite3_step(row.value)!=SQLITE_ROW||!read_reward(row.value,debt))return false;
    return sqlite3_step(row.value)==SQLITE_DONE;
}

[[nodiscard]] bool select_reward_by_id(std::uint64_t debtId,RewardDebt& debt) noexcept {
    if(debtId==0||debtId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))return false;
    Statement row{"SELECT debt_id,account_soid,character_soid,runtime_epoch,session_id,run_id,mission_hash,definition_hash,quantity,credited,delivered FROM reward_debts WHERE debt_id=?1"};
    if(!row.ready()||!bind_i64(row.value,1,static_cast<std::int64_t>(debtId))
        ||sqlite3_step(row.value)!=SQLITE_ROW||!read_reward(row.value,debt))return false;
    return sqlite3_step(row.value)==SQLITE_DONE;
}

[[nodiscard]] bool mark_reward_delivered(const RewardDebt& debt,std::int32_t credited) noexcept {
    if(debt.debtId==0||debt.delivered||credited<0||credited>debt.quantity)return false;
    Statement update{"UPDATE reward_debts SET delivered=1,credited=?1 WHERE debt_id=?2 AND delivered=0"};
    return update.ready()&&sqlite3_bind_int(update.value,1,credited)==SQLITE_OK
        &&bind_i64(update.value,2,static_cast<std::int64_t>(debt.debtId))
        &&step_done(update.value)&&sqlite3_changes(database)==1;
}
}

bool offer_reward(std::uint64_t accountSoid,std::uint64_t characterSoid,
                  std::uint64_t sessionId,std::uint64_t runId,std::uint32_t missionHash,
                  std::uint32_t definitionHash,std::int32_t quantity,std::int64_t updatedUtc,
                  RewardDebt& debt) noexcept {
    Lock lock;debt={};
    if(!accountSoid||!characterSoid||!sessionId||!runId||!missionHash||!definitionHash
        ||quantity<=0||updatedUtc<0)return false;
    if(memoryOnly) {
        debt={1,accountSoid,characterSoid,1,sessionId,runId,missionHash,definitionHash,quantity,0,false};
        return true;
    }
    if(database==nullptr||!owner_exists(Scope::account,accountSoid)
        ||!owner_exists(Scope::character,characterSoid)||!begin())return false;
    const auto previous=accountRevision;
    Statement insert{"INSERT OR IGNORE INTO reward_debts(account_soid,character_soid,mission_hash,runtime_epoch,session_id,run_id,definition_hash,quantity) VALUES(?1,?2,?3,?4,?5,?6,?7,?8)"};
    const bool inserted=insert.ready()&&bind_u64(insert.value,1,accountSoid)
        &&bind_u64(insert.value,2,characterSoid)&&bind_i64(insert.value,3,missionHash)
        &&bind_u64(insert.value,4,rewardEpoch)&&bind_u64(insert.value,5,sessionId)
        &&bind_u64(insert.value,6,runId)&&bind_i64(insert.value,7,definitionHash)
        &&sqlite3_bind_int(insert.value,8,quantity)==SQLITE_OK&&step_done(insert.value);
    if(!inserted||!select_reward_by_key(accountSoid,sessionId,runId,definitionHash,debt)
        ||debt.characterSoid!=characterSoid||debt.missionHash!=missionHash
        ||debt.quantity!=quantity) {
        rollback();accountRevision=previous;debt={};return false;
    }
    Statement completeMission{"UPDATE missions SET completed=1,updated_utc=CASE WHEN updated_utc<?1 THEN ?1 ELSE updated_utc END WHERE character_soid=?2 AND mission_hash=?3"};
    if(!completeMission.ready()||!bind_i64(completeMission.value,1,updatedUtc)
        ||!bind_u64(completeMission.value,2,characterSoid)
        ||!bind_i64(completeMission.value,3,missionHash)||!step_done(completeMission.value)
        ||sqlite3_changes(database)!=1||!advance_revision()||!commit()) {
        rollback();accountRevision=previous;debt={};return false;
    }
    return true;
}

bool load_pending_reward(std::uint64_t accountSoid,bool& found,RewardDebt& debt) noexcept {
    Lock lock;found=false;debt={};if(memoryOnly)return true;
    if(database==nullptr||!owner_exists(Scope::account,accountSoid))return false;
    Statement row{"SELECT debt_id,account_soid,character_soid,runtime_epoch,session_id,run_id,mission_hash,definition_hash,quantity,credited,delivered FROM reward_debts WHERE account_soid=?1 AND delivered=0 ORDER BY debt_id LIMIT 1"};
    if(!row.ready()||!bind_u64(row.value,1,accountSoid))return false;
    const int result=sqlite3_step(row.value);if(result==SQLITE_DONE)return true;
    if(result!=SQLITE_ROW||!read_reward(row.value,debt)||debt.delivered)return false;
    found=true;return sqlite3_step(row.value)==SQLITE_DONE;
}

bool finish_reward(std::uint64_t debtId,std::int32_t credited) noexcept {
    Lock lock;if(credited!=0)return false;if(memoryOnly)return true;if(database==nullptr||!begin())return false;
    const auto previous=accountRevision;RewardDebt debt{};
    if(!select_reward_by_id(debtId,debt)) {rollback();return false;}
    if(debt.delivered) {const bool matches=debt.credited==credited;rollback();return matches;}
    if(!mark_reward_delivered(debt,credited)||!advance_revision()||!commit()) {
        rollback();accountRevision=previous;return false;
    }
    return true;
}

bool commit_account_and_reward(const AccountState& before,const AccountState& after,
                               std::uint64_t debtId,std::int32_t credited) noexcept {
    Lock lock;if(!account::valid(before)||!account::valid(after))return false;
    if(memoryOnly)return true;if(database==nullptr||!begin())return false;
    const auto previous=accountRevision;RewardDebt debt{};
    if(!select_reward_by_id(debtId,debt)||debt.delivered||debt.accountSoid!=before.primarySoid
        ||credited<=0||credited>debt.quantity||!migrate_owners(before,after)
        ||!write_account(after)||!mark_reward_delivered(debt,credited)
        ||!advance_revision()||!commit()) {
        rollback();accountRevision=previous;return false;
    }
    return true;
}

} // namespace dawn::state::persistence
