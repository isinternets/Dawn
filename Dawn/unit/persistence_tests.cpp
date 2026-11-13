#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "../src/core/logging/log.h"
#include "../src/state/activity/Newlight/launchpad/quest.h"
#include "../src/core/settings/settings.h"
#include "../src/state/persistence/persistence.h"
#include "../src/state/runtime/state.h"
#include "../src/state/activity/progress/mission_progress.h"
#include "../src/state/activity/nightfall/completion_reward.h"
#include "../vendor/sqlite/sqlite3.h"

#define CHECK(expression) do { if (!(expression)) { std::fprintf(stderr,"CHECK failed at line %d: %s\n",__LINE__,#expression); std::abort(); } } while (false)

namespace dawn::core::log {
void write(Channel,Level,std::string_view message) noexcept {
    std::fprintf(stderr,"%.*s\n",static_cast<int>(message.size()),message.data());
}
void early(std::string_view) noexcept {}
Settings defaults() noexcept { return {}; }
}

namespace {

/** The text of a settings document without its character templates, as an older release wrote it. */
std::string without_character_templates(std::string json) {
    const std::size_t key=json.find("\"character_templates\"");
    CHECK(key!=std::string::npos);
    const std::size_t open=json.find('[',key);
    CHECK(open!=std::string::npos);
    int depth=0;std::size_t close=open;
    for(;close<json.size();++close) {
        if(json[close]=='[')++depth;
        else if(json[close]==']'&&--depth==0)break;
    }
    CHECK(close<json.size());
    std::size_t begin=key,end=close+1;
    const std::size_t next=json.find_first_not_of(" \t\r\n",end);
    if(next!=std::string::npos&&json[next]==',') {
        end=next+1;
    } else {
        // Last member of its object: the comma before it goes instead, or one is left dangling.
        const std::size_t previous=json.find_last_not_of(" \t\r\n",key-1);
        CHECK(previous!=std::string::npos&&json[previous]==',');
        begin=previous;
    }
    json.erase(begin,end-begin);
    return json;
}
using namespace dawn::state;

AccountState make_account() {
    AccountState value{};
    value.primarySoid=0xF100000000000001ULL;
    value.characterCount=2;
    auto& character=value.characters[0];
    character.soid=0xF100000000000002ULL;character.race=CharacterRace::awoken;
    character.gender=CharacterGender::female;character.characterClass=CharacterClass::hunter;
    character.level=20;character.accepted=true;character.previewAvailable=true;
    character.appearanceValue=2.5F;character.lastOrbitedDestination=0xF1234567U;
    character.inventory.count=1;character.nextInventorySerial=12;
    auto& item=character.inventory.values[0];item.instanceSoid=0x4000000000000100ULL;
    item.definitionHash=0xF2345678U;item.level=200;item.quantity=1;item.mutationSerial=11;
    item.flags=account::inventory::kLockedItemFlag;item.sockets.policy=account::inventory::SocketPolicy::authored;
    item.sockets.plugCount=2;item.sockets.plugs[0]=0xF3456789U;
    value.profileItemCount=1;value.profileItems[0].definitionHash=0xF456789AU;
    value.profileItems[0].quantity=100;value.profileItems[0].mutationSerial=4;
    auto& settings=value.settings;settings.configured=true;settings.keyBindings.configured=true;
    settings.controls.mouseLookSensitivity=1;settings.controls.adsSensitivityModifier=1.0F;
    settings.audio.migrationVersion=account::settings::kCompletedAudioMigrationVersion;
    settings.display.calibrationPrimary=10000.0F;
    value.characters[1].soid=0xF100000000000003ULL;
    value.characters[1].characterClass=CharacterClass::warlock;
    return value;
}

std::filesystem::path database_path() {
    wchar_t path[32768]{};const DWORD length=GetModuleFileNameW(nullptr,path,32768);CHECK(length>0&&length<32768);
    return std::filesystem::path(path).parent_path()/L"Dawn"/L"player-state.db";
}

void remove_database() {
    const auto path=database_path();
    CHECK(path.parent_path().filename()==L"Dawn");
    CHECK(path.parent_path().parent_path().filename()==L"persistence");
    std::error_code ignored;std::filesystem::remove(path,ignored);
    std::filesystem::remove(path.wstring()+L"-wal",ignored);std::filesystem::remove(path.wstring()+L"-shm",ignored);
}

void run_fresh_process_verifier() {
    wchar_t executable[32768]{};CHECK(GetModuleFileNameW(nullptr,executable,32768)>0);
    std::wstring command=L"\"";command+=executable;command+=L"\" --verify";
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=FALSE);
    CHECK(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0);DWORD exitCode=1;
    CHECK(GetExitCodeProcess(process.hProcess,&exitCode)!=FALSE);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    CHECK(exitCode==0);
}

void edit_database(const char* sql) {
    sqlite3* connection{};CHECK(sqlite3_open16(database_path().c_str(),&connection)==SQLITE_OK);
    char* error{};
    const int result=sqlite3_exec(connection,sql,nullptr,nullptr,&error);
    if(error) std::fprintf(stderr,"%s\n",error);
    sqlite3_free(error);sqlite3_close(connection);CHECK(result==SQLITE_OK);
}

int database_integer(const char* sql) {
    sqlite3* connection{};CHECK(sqlite3_open16(database_path().c_str(),&connection)==SQLITE_OK);
    sqlite3_stmt* statement{};CHECK(sqlite3_prepare_v2(connection,sql,-1,&statement,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(statement)==SQLITE_ROW);const int value=sqlite3_column_int(statement,0);
    CHECK(sqlite3_step(statement)==SQLITE_DONE);sqlite3_finalize(statement);sqlite3_close(connection);return value;
}

void test_editor_backup() {
    const int quantity = database_integer("SELECT quantity FROM profile_items WHERE position=0");
    const int revision = database_integer("SELECT value FROM metadata WHERE key='account_revision'");
    CHECK(dawn::state::persistence::backup_for_editor());
    const auto directory = database_path().parent_path() / L"editor-backups";
    std::filesystem::path latest;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == L".db" && (latest.empty() || entry.path().filename() > latest.filename())) latest = entry.path();
    CHECK(!latest.empty());
    sqlite3* copy{}; CHECK(sqlite3_open16(latest.c_str(), &copy) == SQLITE_OK);
    sqlite3_stmt* statement{};
    CHECK(sqlite3_prepare_v2(copy,"PRAGMA integrity_check",-1,&statement,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(statement)==SQLITE_ROW);
    CHECK(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(statement,0)))=="ok");
    sqlite3_finalize(statement);
    CHECK(sqlite3_prepare_v2(copy,"SELECT quantity FROM profile_items WHERE position=0",-1,&statement,nullptr)==SQLITE_OK);
    CHECK(sqlite3_step(statement)==SQLITE_ROW && sqlite3_column_int(statement,0)==quantity);
    sqlite3_finalize(statement); sqlite3_close(copy);
    CHECK(database_integer("SELECT value FROM metadata WHERE key='account_revision'")==revision);
}

void test_vendor_migrations(const AccountState& legacy,const unlocks::Table& initialUnlocks,
                            const Family5State& family) {
    namespace durable=dawn::state::persistence;
    AccountState loaded{};unlocks::ScopedTable unlocks{};Family5State loadedFamily{};
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    durable::shutdown();
    // Original v1 saves still upgrade through the production import path.
    edit_database("ALTER TABLE characters DROP COLUMN vendor_campaigns;"
                  "ALTER TABLE character_items DROP COLUMN postmaster;"
                  "DROP TABLE vendor_progress;DROP TABLE vendor_unlocks;PRAGMA user_version=1;");
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==legacy);
    AccountState expected=legacy;
    expected.vendorProgress[3]={20,6000,1};
    expected.characters[0].vendorProgress[7]={11,4000,1};
    expected.vendorUnlocks.flags.push_back({91,2});expected.vendorUnlocks.values.push_back({92,1234});
    expected.characters[0].vendorUnlocks.flags.push_back({91,1});
    expected.characters[1].vendorUnlocks.values.push_back({92,5678});
    namespace newlight=dawn::state::activity::newlight::launchpad::quest;
    CHECK(newlight::record_escape(expected.characters[1]));
    expected.characters[0].vendorCampaigns=3;
    expected.characters[0].inventory.values[0].postmaster=true;
    CHECK(durable::commit_account(loaded,expected));durable::shutdown();
    // Vendor-branch v2 uses the same codec as v3 and needs no data conversion.
    edit_database("PRAGMA user_version=2");
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==expected);
    CHECK(newlight::escaped(loaded.characters[1]) && !newlight::escaped(loaded.characters[0]));
    std::array<std::byte,60> startFlags{};startFlags[20]=std::byte{2};
    newlight::project_start(loaded.characters[1],startFlags);
    CHECK(startFlags[20]==std::byte{} && startFlags[59]==std::byte{2});
    durable::shutdown();
    CHECK(database_integer("PRAGMA user_version")==5);
    // Recreate the old local branch's exact scoped layout, including all unused slots.
    edit_database(R"sql(
ALTER TABLE vendor_progress RENAME TO current_progress;
ALTER TABLE vendor_unlocks RENAME TO current_unlocks;
CREATE TABLE vendor_progress(scope INTEGER NOT NULL,owner_soid TEXT NOT NULL,position INTEGER NOT NULL,vendor INTEGER NOT NULL,points INTEGER NOT NULL,rewards INTEGER NOT NULL,PRIMARY KEY(scope,owner_soid,position));
CREATE TABLE vendor_unlocks(scope INTEGER NOT NULL,owner_soid TEXT NOT NULL,numeric INTEGER NOT NULL,position INTEGER NOT NULL,slot INTEGER NOT NULL,value INTEGER NOT NULL,PRIMARY KEY(scope,owner_soid,numeric,position));
WITH RECURSIVE positions(position) AS (SELECT 0 UNION ALL SELECT position+1 FROM positions WHERE position<15),
 owners(scope,soid) AS (SELECT 0,primary_soid FROM account UNION ALL SELECT 1,soid FROM characters)
INSERT INTO vendor_progress SELECT scope,soid,positions.position,COALESCE(vendor,65535),COALESCE(points,0),COALESCE(rewards,0)
 FROM owners CROSS JOIN positions LEFT JOIN current_progress p ON p.owner_soid=soid AND p.position=positions.position;
INSERT INTO vendor_unlocks SELECT CASE WHEN owner_soid=(SELECT primary_soid FROM account) THEN 0 ELSE 1 END,owner_soid,kind,position,slot,value FROM current_unlocks;
DROP TABLE current_progress;DROP TABLE current_unlocks;
UPDATE vendor_progress SET vendor=324 WHERE vendor=11;
PRAGMA user_version=2;
)sql");
    // Refuse to lose nonempty sentinel data or silently reassign an owner scope.
    edit_database("UPDATE vendor_progress SET points=1 WHERE vendor=65535");
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(database_integer("PRAGMA user_version")==2);
    CHECK(database_integer("SELECT COUNT(*) FROM vendor_progress WHERE vendor=65535 AND points=1")==46);
    edit_database("UPDATE vendor_progress SET points=0 WHERE vendor=65535;UPDATE vendor_unlocks SET scope=1 WHERE scope=0");
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    const int movement=database_integer("SELECT movement_ability FROM characters WHERE position=0");
    edit_database("UPDATE vendor_unlocks SET scope=0 WHERE owner_soid=(SELECT primary_soid FROM account);"
                  "UPDATE characters SET movement_ability=256 WHERE position=0");
    // Even failure after the schema conversion must roll back the entire migration.
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(database_integer("PRAGMA user_version")==2);
    CHECK(database_integer("SELECT COUNT(*) FROM vendor_progress WHERE vendor=65535")==46);
    CHECK(database_integer("SELECT COUNT(*) FROM vendor_unlocks WHERE numeric=1")==2);
    edit_database(("UPDATE characters SET movement_ability="+std::to_string(movement)+" WHERE position=0").c_str());
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==expected);CHECK(unlocks==unlocks::expand(initialUnlocks,expected));CHECK(loadedFamily==family);
    CHECK(durable::commit_account(loaded,loaded));durable::shutdown();
    CHECK(database_integer("PRAGMA user_version")==5);
    CHECK(database_integer("SELECT COUNT(*) FROM vendor_progress")==2);
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==expected);durable::shutdown();
    // Upgrade our deployed v3 save without modifying inventory or New Light progress.
    edit_database("DROP TABLE item_rolls;PRAGMA user_version=3;");
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==expected);
    auto rolled=loaded;
    auto& item=rolled.characters[0].inventory.values[0];
    item.randomRoll={7,19,31,43,59,71,89,101};
    item.rolledLaneMask=3;item.availablePlugRows[0]=5;item.availablePlugRows[1]=9;
    CHECK(durable::commit_account(loaded,rolled));durable::shutdown();
    CHECK(database_integer("PRAGMA user_version")==5);
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,initialUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded==rolled);CHECK(newlight::escaped(loaded.characters[1]));
    durable::shutdown();remove_database();
}
}

int main(int argc,char** argv) {
    using namespace dawn::state;
    namespace durable=dawn::state::persistence;
    std::printf("sizeof(AccountState)=%zu sizeof(State)=%zu sizeof(ScopedTable)=%zu "
                "sizeof(Family5State)=%zu sizeof(ActivityDefaults)=%zu\n",
                sizeof(AccountState),sizeof(State),sizeof(unlocks::ScopedTable),
                sizeof(Family5State),sizeof(activity::defaults::ActivityDefaults));
    // Exercise the shipped JSON fixture through the production parser before handing its exact
    // authored account/unlock/investment structures to the first-import path.
    const auto fixturePath=std::filesystem::path(__FILE__).parent_path().parent_path()
        /L"resources"/L"default_settings.json";
    std::ifstream fixture(fixturePath,std::ios::binary);CHECK(fixture.good());
    const std::string fixtureJson{std::istreambuf_iterator<char>{fixture},
                                  std::istreambuf_iterator<char>{}};
    dawn::core::settings::Settings fixtureSettings{};
    CHECK(dawn::core::settings::parse(fixtureJson,fixtureSettings));
    CHECK(fixtureSettings.initialAccount.primarySoid==0x9EAA300100100100ULL);
    // The shipped settings seed no character. The player creates the first one, which is built
    // from one of these templates, one per class.
    CHECK(fixtureSettings.initialAccount.characterCount==0);
    CHECK(fixtureSettings.characterTemplates.characterCount==3);
    CHECK(fixtureSettings.characterTemplates.characters[0].soid==0x9EAA300100100101ULL);
    CHECK(fixtureSettings.characterTemplates.characters[0].characterClass==CharacterClass::hunter);
    CHECK(fixtureSettings.characterTemplates.characters[1].characterClass==CharacterClass::titan);
    CHECK(fixtureSettings.characterTemplates.characters[2].characterClass==CharacterClass::warlock);
    CHECK(std::count(fixtureSettings.initialUnlocks.accountFlags.begin(),
                     fixtureSettings.initialUnlocks.accountFlags.end(),
                     unlocks::kFlagSet)!=0);
    CHECK(fixtureSettings.initialFamily5.flagCount!=0);
    {
        // The updater keeps the settings file of an older release, which has no character templates.
        // It still parses, and the templates come from the bundled defaults, one per class, unless
        // the file carries its own.
        const std::string olderJson=without_character_templates(fixtureJson);
        CHECK(olderJson.size()<fixtureJson.size());
        static dawn::core::settings::Settings older{};
        CHECK(dawn::core::settings::parse(olderJson,older));
        CHECK(older.characterTemplates.characterCount==0);
        CHECK(dawn::core::settings::fill_missing_character_templates(older,fixtureJson));
        CHECK(older.characterTemplates==fixtureSettings.characterTemplates);
        static dawn::core::settings::Settings custom{};
        custom=fixtureSettings;
        custom.characterTemplates.characterCount=1;
        CHECK(dawn::core::settings::fill_missing_character_templates(custom,fixtureJson));
        CHECK(custom.characterTemplates.characterCount==1);
        static dawn::core::settings::Settings unfilled{};
        CHECK(dawn::core::settings::parse(olderJson,unfilled));
        CHECK(!dawn::core::settings::fill_missing_character_templates(unfilled,olderJson));
        CHECK(unfilled.characterTemplates.characterCount==0);
    }

    const AccountState legacy=make_account();CHECK(account::valid(legacy));
    unlocks::Table legacyUnlocks{};legacyUnlocks.accountFlags[120]=unlocks::kFlagSet;
    legacyUnlocks.characterObjectValues[33]=77;legacyUnlocks.characterProgressions[8][2]=19;
    Family5State family{};family.flagCount=1;family.flags[0]={91,2};family.valueCount=1;family.values[0]={17,900};
    AccountState loaded{};unlocks::ScopedTable unlocks{};Family5State loadedFamily{};
    if(argc==2 && std::string_view(argv[1])=="--load-existing") {
        CHECK(database_path().parent_path().parent_path().filename()==L"persistence");
        CHECK(std::filesystem::exists(database_path()));
        if(!durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily)) return 1;
        std::printf("Existing save loaded: %zu characters, %zu profile items; account valid=%d\n",
                    static_cast<std::size_t>(loaded.characterCount),static_cast<std::size_t>(loaded.profileItemCount),
                    account::valid(loaded));
        durable::shutdown();return 0;
    }
    if(argc>1) {
        CHECK(argc==2 && std::string_view(argv[1])=="--verify");
        CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
        CHECK(loaded.profileItems[0].quantity==100&&loaded.characters[0].inventory.values[0].flags==0);
        bool found=false;durable::MissionRecord mission{};
        CHECK(durable::load_mission(loaded.characters[0].soid,0xF9876543U,found,mission));
        CHECK(found&&mission.checkpointSliceSet==41);durable::shutdown();return 0;
    }
    // Gives a character its key and item instance keys of its own, so two of them never collide.
    const auto reseed=[](CharacterState& character,std::uint64_t soid,std::uint64_t firstItem) {
        character.soid=soid;
        for(std::optional<account::inventory::Item>& item:character.equipment.slots)
            if(item.has_value())item->instanceSoid=firstItem++;
        for(std::size_t i=0;i<character.inventory.count;++i)
            character.inventory.values[i].instanceSoid=firstItem++;
    };
    CHECK(account::valid(fixtureSettings.initialAccount));
    remove_database();
    CHECK(durable::initialize(GetModuleHandleW(nullptr),fixtureSettings.initialAccount,
        fixtureSettings.initialUnlocks,fixtureSettings.initialFamily5,loaded,unlocks,loadedFamily));
    CHECK(loaded==fixtureSettings.initialAccount);
    CHECK(unlocks==unlocks::expand(fixtureSettings.initialUnlocks,
                                   fixtureSettings.initialAccount));
    CHECK(loadedFamily==fixtureSettings.initialFamily5);
    {
        // The shipped account starts with no character. Creating one grows the stored account, and
        // that has to commit and read back. Dropping one would strand its rows, so that stays refused.
        static AccountState created{};created=loaded;
        created.characters[0]=fixtureSettings.characterTemplates.characters[1];
        created.characters[0].soid=created.primarySoid+1U;
        created.characterCount=1;
        CHECK(account::valid(created));
        CHECK(durable::commit_account(loaded,created));
        CHECK(!durable::commit_account(created,loaded));
        {
            // Deleting a character removes its row and every durable row it owned, and leaves the
            // others exactly as they were, under the keys they already had.
            static AccountState two{};two=created;
            reseed(two.characters[0],two.primarySoid+1U,0x7000000000000000ULL);
            two.characters[1]=fixtureSettings.characterTemplates.characters[2];
            reseed(two.characters[1],two.primarySoid+2U,0x7100000000000000ULL);
            two.characterCount=2;
            CHECK(account::valid(two));
            CHECK(durable::commit_account(created,two));
            const std::uint64_t first=two.characters[0].soid,second=two.characters[1].soid;
            CHECK(durable::store_objective(durable::Scope::characterObject,first,5,11));
            CHECK(durable::store_objective(durable::Scope::characterObject,second,5,22));
            static AccountState gone{};gone=two;
            gone.characters[0]=two.characters[1];gone.characters[1]={};gone.characterCount=1;
            CHECK(account::valid(gone));
            // Only the character actually removed may leave, and the survivors must not move.
            CHECK(!durable::commit_character_removal(two,gone,0));
            CHECK(!durable::commit_character_removal(two,gone,first+0x10U));
            CHECK(!durable::commit_character_removal(two,two,first));
            CHECK(!durable::commit_character_removal(two,gone,second));
            CHECK(durable::commit_character_removal(two,gone,first));
            // The removed character no longer exists, so its rows are counted straight from the file.
            const auto ownerRows=[](std::uint64_t owner) {
                sqlite3* connection{};CHECK(sqlite3_open16(database_path().c_str(),&connection)==SQLITE_OK);
                sqlite3_stmt* count{};char text[17]{};
                std::snprintf(text,sizeof text,"%016llX",static_cast<unsigned long long>(owner));
                CHECK(sqlite3_prepare_v2(connection,"SELECT COUNT(*) FROM durable_objectives WHERE owner_soid=?1",-1,&count,nullptr)==SQLITE_OK);
                CHECK(sqlite3_bind_text(count,1,text,-1,SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_step(count)==SQLITE_ROW);
                const int rows=sqlite3_column_int(count,0);
                sqlite3_finalize(count);sqlite3_close(connection);return rows;
            };
            bool foundKept=false;std::int32_t keptValue=0,ignored=0;
            CHECK(ownerRows(first)==0);
            CHECK(ownerRows(second)==1);
            CHECK(durable::load_objective(durable::Scope::characterObject,second,5,foundKept,keptValue));
            CHECK(foundKept&&keptValue==22);
            // The freed key is reusable and must not inherit anything from its previous owner.
            static AccountState regrown{};regrown=gone;
            regrown.characters[1]=fixtureSettings.characterTemplates.characters[1];
            reseed(regrown.characters[1],first,0x7200000000000000ULL);
            regrown.characterCount=2;
            CHECK(account::valid(regrown));
            CHECK(durable::commit_account(gone,regrown));
            bool foundReused=true;
            CHECK(durable::load_objective(durable::Scope::characterObject,first,5,foundReused,ignored));
            CHECK(!foundReused&&ownerRows(first)==0);
            CHECK(durable::commit_character_removal(regrown,gone,first));
            // Removing the last remaining character leaves an empty, valid account.
            static AccountState empty{};empty=gone;empty.characters[0]={};empty.characterCount=0;
            CHECK(account::valid(empty));
            CHECK(durable::commit_character_removal(gone,empty,second));
            CHECK(durable::commit_account(empty,created));
        }
        {
            // A save that went through a deletion holds the survivor under its old key. Opening it
            // rebases that key, and the rows the survivor owned follow it.
            static AccountState two{};two=created;
            reseed(two.characters[0],two.primarySoid+1U,0x7000000000000000ULL);
            two.characters[1]=fixtureSettings.characterTemplates.characters[2];
            reseed(two.characters[1],two.primarySoid+2U,0x7100000000000000ULL);
            two.characterCount=2;
            CHECK(account::valid(two));
            CHECK(durable::commit_account(created,two));
            const std::uint64_t first=two.characters[0].soid,second=two.characters[1].soid;
            CHECK(durable::store_objective(durable::Scope::characterObject,second,5,22));
            static AccountState gone{};gone=two;
            gone.characters[0]=two.characters[1];gone.characters[1]={};gone.characterCount=1;
            CHECK(durable::commit_character_removal(two,gone,first));
            durable::shutdown();
            static AccountState reopened{};static unlocks::ScopedTable reopenedUnlocks{};static Family5State reopenedFamily{};
            CHECK(durable::initialize(GetModuleHandleW(nullptr),fixtureSettings.initialAccount,
                fixtureSettings.initialUnlocks,fixtureSettings.initialFamily5,reopened,reopenedUnlocks,reopenedFamily));
            CHECK(reopened.characterCount==1&&reopened.characters[0].soid==first);
            CHECK(reopenedUnlocks.characters[0].characterSoid==first);
            bool found=false;std::int32_t value=0;
            CHECK(durable::load_objective(durable::Scope::characterObject,first,5,found,value));
            CHECK(found&&value==22);
            // The renamed save is stored as it was rebased, so a second open changes nothing.
            durable::shutdown();
            static AccountState again{};static unlocks::ScopedTable againUnlocks{};static Family5State againFamily{};
            CHECK(durable::initialize(GetModuleHandleW(nullptr),fixtureSettings.initialAccount,
                fixtureSettings.initialUnlocks,fixtureSettings.initialFamily5,again,againUnlocks,againFamily));
            CHECK(again==reopened);
            CHECK(durable::commit_account(again,created));
        }
        durable::shutdown();
        static AccountState reloaded{};static unlocks::ScopedTable reloadedUnlocks{};static Family5State reloadedFamily{};
        CHECK(durable::initialize(GetModuleHandleW(nullptr),fixtureSettings.initialAccount,
            fixtureSettings.initialUnlocks,fixtureSettings.initialFamily5,reloaded,reloadedUnlocks,reloadedFamily));
        CHECK(reloaded==created);
        durable::shutdown();
        remove_database();
        CHECK(durable::initialize(GetModuleHandleW(nullptr),fixtureSettings.initialAccount,
            fixtureSettings.initialUnlocks,fixtureSettings.initialFamily5,loaded,unlocks,loadedFamily));
    }
    durable::shutdown();
    remove_database();
    test_vendor_migrations(legacy,legacyUnlocks,family);
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded.primarySoid==legacy.primarySoid&&loaded.characters[0].inventory.values[0].flags==1);
    test_editor_backup();
    CHECK(unlocks.accountFlags[120]==2&&unlocks.characters[0].objectValues[33]==77);
    CHECK(unlocks.characters[0].progressions[8][2]==19&&loadedFamily.values[0].value==900);
    CHECK(unlocks.characters[1].objectValues[33]==77);

    namespace mission_progress=dawn::state::activity::progress;
    mission_progress::reset();
    for(int index=0;index<40;++index) {
        const std::string package="test_mission_"+std::to_string(index);
        CHECK(mission_progress::observe(loaded.characters[0].soid,package,index+1,
            0xF0000000U+static_cast<std::uint32_t>(index),index,index,false));
    }
    CHECK(mission_progress::observe(loaded.characters[0].soid,"test_mission_complete",90,
        0xF1230000U,50,9,true));
    CHECK(mission_progress::observe(loaded.characters[0].soid,"test_mission_complete",90,
        0xF1230001U,51,1,false));
    bool observerFound=false;durable::MissionRecord observerRecord{};
    CHECK(durable::load_mission(loaded.characters[0].soid,
        mission_progress::mission_key("test_mission_complete"),observerFound,observerRecord));
    CHECK(observerFound&&observerRecord.completed);

    // A failed durable write remains retryable and is never mistaken for a cached success.
    sqlite3* blockedWriter{};CHECK(sqlite3_open16(database_path().c_str(),&blockedWriter)==SQLITE_OK);
    CHECK(sqlite3_exec(blockedWriter,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK);
    CHECK(!mission_progress::observe(loaded.characters[0].soid,"test_mission_retry",91,
        0xF1230002U,52,2,false));
    CHECK(sqlite3_exec(blockedWriter,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(blockedWriter);
    CHECK(mission_progress::observe(loaded.characters[0].soid,"test_mission_retry",91,
        0xF1230002U,52,2,false));

    CHECK(durable::store_flag(durable::Scope::characterObject,loaded.characters[0].soid,99,2));
    durable::MissionRecord mission{};mission.characterSoid=loaded.characters[0].soid;
    mission.missionHash=0xF9876543U;mission.checkpointHash=0xF8765432U;mission.checkpointSliceSet=41;
    mission.activityIndex=7;mission.progress=3;mission.completed=true;mission.updatedUtc=1000;
    CHECK(durable::store_mission(mission));
    AccountState after=loaded;after.characters[0].inventory.values[0].flags=0;
    CHECK(durable::commit_account(loaded,after));loaded=after;
    std::uint64_t next{};CHECK(durable::next_item_instance_soid(loaded,next));
    CHECK(next>0x4000000000000100ULL);
    durable::shutdown();run_fresh_process_verifier();
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));

    // Closing a competing writer with an uncommitted WAL transaction must recover the prior
    // durable image on a genuinely fresh persistence open.
    sqlite3* rival{};CHECK(sqlite3_open16(database_path().c_str(),&rival)==SQLITE_OK);
    CHECK(sqlite3_exec(rival,"BEGIN IMMEDIATE;UPDATE profile_items SET quantity=777 WHERE position=0",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(rival);durable::shutdown();
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded.profileItems[0].quantity==100);

    // A second connection advances the revision. This writer must reject its stale snapshot and
    // roll every table replacement back.
    CHECK(sqlite3_open16(database_path().c_str(),&rival)==SQLITE_OK);
    CHECK(sqlite3_exec(rival,"UPDATE metadata SET value=value+1 WHERE key='account_revision'",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(rival);
    AccountState rejected=loaded;rejected.profileItems[0].quantity=999;
    CHECK(!durable::commit_account(loaded,rejected));
    durable::shutdown();

    AccountState changedLegacy=legacy;changedLegacy.profileItems[0].quantity=1;
    unlocks::Table changedUnlocks{};Family5State changedFamily{};
    CHECK(durable::initialize(GetModuleHandleW(nullptr),changedLegacy,changedUnlocks,changedFamily,loaded,unlocks,loadedFamily));
    CHECK(loaded.profileItems[0].quantity==100&&loaded.characters[0].inventory.values[0].flags==0);
    bool found=false;std::uint8_t flag{};CHECK(durable::load_flag(durable::Scope::characterObject,loaded.characters[0].soid,99,found,flag));
    CHECK(found&&flag==2);
    CHECK(durable::load_flag(durable::Scope::characterObject,loaded.characters[1].soid,99,found,flag));
    CHECK(!found);
    durable::MissionRecord restored{};CHECK(durable::load_mission(loaded.characters[0].soid,mission.missionHash,found,restored));
    CHECK(found&&restored.completed&&restored.checkpointSliceSet==41&&restored.activityIndex==7);

    // Identity rebases may overlap the previous namespace (P->C0, C0->C1). Every scoped
    // row must move exactly once rather than being remapped again by a later update.
    CHECK(durable::store_flag(durable::Scope::characterObject,loaded.characters[1].soid,99,1));
    const std::uint64_t oldPrimary=loaded.primarySoid;
    const std::uint64_t oldCharacter0=loaded.characters[0].soid;
    const std::uint64_t oldCharacter1=loaded.characters[1].soid;
    AccountState rebased=loaded;
    rebased.primarySoid=oldCharacter0;
    rebased.characters[0].soid=oldCharacter1;
    rebased.characters[1].soid=oldCharacter1+1;
    CHECK(durable::commit_account(loaded,rebased));loaded=rebased;
    CHECK(durable::load_flag(durable::Scope::account,loaded.primarySoid,120,found,flag));
    CHECK(found&&flag==unlocks::kFlagSet);
    CHECK(durable::load_flag(durable::Scope::characterObject,loaded.characters[0].soid,99,found,flag));
    CHECK(found&&flag==2);
    CHECK(durable::load_flag(durable::Scope::characterObject,loaded.characters[1].soid,99,found,flag));
    CHECK(found&&flag==1);
    CHECK(!durable::load_flag(durable::Scope::account,oldPrimary,120,found,flag));
    CHECK(durable::load_mission(loaded.characters[0].soid,mission.missionHash,found,restored));
    CHECK(found&&restored.completed);

    // Completion is an earned-history bit: a replay checkpoint must never clear it.
    restored.completed=false;restored.progress=1;restored.updatedUtc=1001;
    CHECK(durable::store_mission(restored));
    CHECK(durable::load_mission(loaded.characters[0].soid,mission.missionHash,found,restored));
    CHECK(found&&restored.completed);

    // A terminal creates its debt and completion together. Exact callbacks deduplicate inside one
    // process epoch, while the same process-local counters after a restart name a legitimate replay.
    durable::MissionRecord rewardedMission{};
    rewardedMission.characterSoid=loaded.characters[0].soid;
    rewardedMission.missionHash=0xF9876500U;rewardedMission.activityIndex=808;
    rewardedMission.progress=9;rewardedMission.updatedUtc=1100;
    CHECK(durable::store_mission(rewardedMission));
    durable::RewardDebt firstDebt{},duplicateDebt{};
    CHECK(durable::offer_reward(loaded.primarySoid,loaded.characters[0].soid,0xA001,7,
        rewardedMission.missionHash,0xBC53E66EU,1000,1101,firstDebt));
    CHECK(durable::offer_reward(loaded.primarySoid,loaded.characters[0].soid,0xA001,7,
        rewardedMission.missionHash,0xBC53E66EU,1000,1102,duplicateDebt));
    CHECK(firstDebt.debtId==duplicateDebt.debtId);
    CHECK(durable::load_mission(loaded.characters[0].soid,rewardedMission.missionHash,found,restored));
    CHECK(found&&restored.completed);
    namespace reward=dawn::state::activity::nightfall::rewards;
    reward::Ticket retryTicket{};
    for(std::size_t retry=0;retry<reward::detail::kCapacity+4;++retry) {
        CHECK(reward::claim(0xA001,loaded.primarySoid,retryTicket));
        reward::release(retryTicket);
    }
    CHECK(reward::claim(0xA001,loaded.primarySoid,retryTicket));
    reward::release(retryTicket);
    reward::clear();
    durable::shutdown();
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    durable::RewardDebt replayDebt{};
    CHECK(durable::offer_reward(loaded.primarySoid,loaded.characters[0].soid,0xA001,7,
        rewardedMission.missionHash,0xBC53E66EU,1000,1200,replayDebt));
    CHECK(replayDebt.debtId!=firstDebt.debtId&&replayDebt.runtimeEpoch!=firstDebt.runtimeEpoch);
    CHECK(!durable::finish_reward(firstDebt.debtId,1));
    CHECK(durable::finish_reward(firstDebt.debtId,0));
    CHECK(durable::finish_reward(replayDebt.debtId,0));

    durable::RewardDebt creditedDebt{};
    CHECK(durable::offer_reward(loaded.primarySoid,loaded.characters[0].soid,0xA001,8,
        rewardedMission.missionHash,0xBC53E66EU,1000,1201,creditedDebt));
    AccountState creditedAccount=loaded;
    creditedAccount.profileItems[0].quantity+=1000;
    ++creditedAccount.profileItems[0].mutationSerial;
    CHECK(durable::commit_account_and_reward(loaded,creditedAccount,creditedDebt.debtId,1000));
    loaded=creditedAccount;
    durable::RewardDebt pending{};
    CHECK(durable::load_pending_reward(loaded.primarySoid,found,pending)&&!found);

    AccountState dismantled=loaded;dismantled.characters[0].inventory.values[0]={};
    dismantled.characters[0].inventory.count=0;
    CHECK(durable::commit_account(loaded,dismantled));
    durable::shutdown();

    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(loaded.characters[0].inventory.count==0);
    CHECK(durable::next_item_instance_soid(loaded,next)&&next>0x4000000000000100ULL);
    durable::shutdown();

    // SQLite's conversion APIs narrow permissively; reject out-of-range stored values instead
    // of wrapping malformed durable data into a valid-looking runtime field.
    CHECK(sqlite3_open16(database_path().c_str(),&rival)==SQLITE_OK);
    CHECK(sqlite3_exec(rival,"UPDATE characters SET movement_ability=256 WHERE position=0",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(rival);
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(sqlite3_open16(database_path().c_str(),&rival)==SQLITE_OK);
    CHECK(sqlite3_exec(rival,"UPDATE characters SET movement_ability=0 WHERE position=0",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(rival);
    CHECK(durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    durable::shutdown();

    // A known-newer database is rejected without resetting its contents.
    CHECK(sqlite3_open16(database_path().c_str(),&rival)==SQLITE_OK);
    CHECK(sqlite3_exec(rival,"PRAGMA user_version=99",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(rival);
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(std::filesystem::file_size(database_path())>0);

    // An existing zero-byte file is treated as malformed rather than as a first-run database.
    remove_database();std::filesystem::create_directories(database_path().parent_path());
    { std::ofstream empty(database_path(),std::ios::binary); }
    CHECK(!durable::initialize(GetModuleHandleW(nullptr),legacy,legacyUnlocks,family,loaded,unlocks,loadedFamily));
    CHECK(std::filesystem::file_size(database_path())==0);
    remove_database();
    return 0;
}
