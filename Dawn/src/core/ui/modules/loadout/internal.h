// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "state/editor/armor_plan.h"
#include "state/editor/edit.h"

namespace dawn::core::ui::modules::loadout::internal {

namespace edit = state::editor;

/** 160 bytes hold any search a player types, including a full item name. */
inline constexpr std::size_t kSearchCapacity = 160;
/** The randomizer offers one checkbox per semantic equipment slot. */
inline constexpr std::size_t kSlotCount = 16;
static_assert(kSlotCount == state::account::inventory::kEquipmentSlotCount,
              "The randomizer slot mask must cover every equipment slot the account carries.");
/** Slot 11 is the subclass. It is named here because three pages reason about it. */
inline constexpr std::size_t kSubclassSlot = 11;
/** Slots 0 to 2 are weapons, 3 to 7 armor; the rest are cosmetic. Used to jump to a category. */
inline constexpr std::size_t kLastWeaponSlot = 2;
inline constexpr std::size_t kLastArmorSlot = 7;
/** Highest authored item level, which the game shows as ten times this. The edit layer owns it. */
inline constexpr int kMaximumItemLevel = edit::kMaximumItemLevel;
/** Range the power field offers, which covers every installed reward tier. */
inline constexpr int kPowerSliderMaximum = 15000;

/**
 * @return The Power the game shows for one authored item level.
 * The account stores a level and the game displays ten Power per level, with a floor. Editing the
 * stored level directly showed 106 where the game shows 1060.
 * @param level Authored item level.
 */
[[nodiscard]] int power_of(int level) noexcept;

/**
 * @return The authored level that displays as the given Power.
 * @param power Power the player typed.
 */
[[nodiscard]] int level_of(int power) noexcept;
/** Armor stat plugs never exceed this, so the target sliders stop there. */
inline constexpr int kMaximumStatTarget = 50;
/**
 * A line of character stats, wherever the editor sets one: the icon's edge, the gap between a
 * stat's parts, the gap between two stats, and the letters a stat keeps when the build offers no
 * icon to show it by. Sundial draws 15px icons, 4px between parts and 10px between stats.
 */
inline constexpr float kStatIconExtent = 15.0F;
inline constexpr float kStatPartGap = 4.0F;
inline constexpr float kStatGap = 10.0F;
inline constexpr std::size_t kStatAbbreviation = 3;

/**
 * Layout taken from Sundial, so the in-game editor reads as the same tool.
 * Sundial is an egui desktop app; these are its own authored values, scaled here for the display
 * like every other Dawn measurement. Names follow Sundial's.
 */
/**
 * At or above this width the inspector is a side workspace; below it, a bottom strip.
 * Sundial's own numbers were 980/540/420/760 over a 500-pixel page, sized for a workspace that
 * held the whole item editor. Power, lock, equip and the socket lanes have since moved on to the
 * card, leaving a header and a short column of actions, so these are re-cut for a slim pane and
 * for a page that must keep whole card columns rather than a band of dead space. The floor still
 * clears the stat editor, whose fixed label column leaves the sliders 129 pixels at 300.
 */
inline constexpr float kSideWorkspaceBreakpoint = 900.0F;
/**
 * The side workspace lies over the page rather than beside it, as the game's own item detail
 * lies over the inventory, so it can take the width a tooltip column needs without costing the
 * grid a column of cards.
 */
inline constexpr float kSideWorkspaceDefaultWidth = 440.0F;
inline constexpr float kSideWorkspaceMinWidth = 340.0F;
inline constexpr float kSideWorkspaceMaxWidth = 600.0F;
/** The page keeps at least this much width uncovered: two minimum-width cards and their gap. */
inline constexpr float kPrimaryWorkspaceMinWidth = 590.0F;
/**
 * Workspace frame margin. Sundial keeps symmetric(12, 8); the pane keeps less, because the item
 * frame inside it carries the tooltip's own padding.
 */
inline constexpr float kWorkspaceMarginX = 6.0F;
inline constexpr float kWorkspaceMarginY = 6.0F;
/** Bottom workspace height: preferred 48% of the space, clamped, then capped at 70%. */
inline constexpr float kBottomWorkspacePreferredFraction = 0.48F;
inline constexpr float kBottomWorkspaceMaximumFraction = 0.70F;
inline constexpr float kBottomWorkspaceFloor = 260.0F;
inline constexpr float kBottomWorkspaceMinimum = 280.0F;
inline constexpr float kBottomWorkspaceMaximum = 360.0F;
/**
 * Item card width. Sundial offers Compact/Standard/Wide (285/335/430 minimums) for a window the
 * player sizes; the Dawn panel is fixed and smaller, so it takes the compact end to fit another
 * column instead of leaving a band of empty panel beside the grid.
 */
inline constexpr float kCardMinimumWidth = 285.0F;
/** Vertical gap between two cards in the same column. */
inline constexpr float kCardColumnSpacing = 3.0F;
/** Picker list heights, which Sundial clamps between these. */
inline constexpr float kPickerMinimumHeight = 320.0F;
inline constexpr float kPickerMaximumHeight = 420.0F;

/**
 * Works out the responsive card grid.
 * The column count is Sundial's: as many minimum-width cards as fit. The cards then fill the row
 * rather than stopping at the maximum width, which is where this departs from Sundial. Sundial
 * runs in a window the player resizes, so trailing space there is space they chose; the Dawn panel
 * is a fixed size, and capping the width left a wide band of dead space beside the cards.
 * @param available Width to fill, in framebuffer pixels.
 * @param spacing Horizontal gap between columns, in framebuffer pixels.
 * @param minimumWidth Narrowest a card may be drawn, which sets the column count.
 * @param columns Receives the column count, at least one.
 * @param cardWidth Receives the width of one card.
 */
void card_grid(float available,
               float spacing,
               float minimumWidth,
               int& columns,
               float& cardWidth) noexcept;

/** How far the background catalog load has got. The frame reads it and nothing else. */
enum class CatalogPhase : std::uint8_t {
    idle,
    loading,
    ready,
    failed,
};

/** How far the icon sweep has got. The page reads it and nothing else. */
enum class IconSweepPhase : std::uint8_t {
    idle,
    running,
    done,
    failed,
    /** The result has been moved into the catalog; nothing more to do this session. */
    finished,
};

/** Views of the editor, in the order their tabs appear. Named as Sundial names them. */
enum class View : std::uint8_t {
    /** Character identity and the equipped loadout, on one scrolling page. */
    characters,
    characterInventory,
    profileInventory,
    armory,
    /** The installed icon container, browsed by hand. Nothing else can name what is in it. */
    icons,
    count,
};

/** Item families the armory browses. They are not the catalog's own gear kinds. */
enum class Category : std::uint8_t {
    weapons,
    armor,
    cosmetics,
    perks,
    /** Whatever the other tabs leave: materials, currencies, consumables and subclasses. */
    other,
    count,
};

/** Order the armory grid lists its results in. */
enum class Sort : std::uint8_t {
    type,
    name,
    rarity,
    count,
};

/** What the inspector is bound to. An instance of zero means a catalog item nobody owns yet. */
struct Selection {
    std::uint32_t definitionHash{};
    std::uint64_t instanceSoid{};

    /** @return True when the given card is the selected one. */
    [[nodiscard]] bool holds(std::uint32_t hash, std::uint64_t instance) const noexcept {
        return definitionHash == hash && instanceSoid == instance;
    }
};

/** Armory filters. Their combined key decides when the result list is rebuilt. */
struct Browse {
    Category category{Category::weapons};
    Sort sort{Sort::type};
    /** Zero shows every rarity; otherwise only this tier. */
    int rarity{};
    bool classOnly{};
    bool includeInternal{};
    /** Equipment slot the results are narrowed to, or -1 for every slot. A swap sets its own. */
    int slot{-1};
    /** Damage type weapons are narrowed to, as an `edit::DamageType` value, or -1 for every one. */
    int damageType{-1};
    /** Ammunition weapons are narrowed to, as an `edit::Ammo` value, or -1 for every one. */
    int ammo{-1};
    /** `armorClass` value that follows the class of the character in play, which is where it starts. */
    static constexpr int kOwnClass = -2;
    /**
     * Class armor is narrowed to, as a `state::CharacterClass` value, or -1 for every class. It stands
     * in for `classOnly` on the armor tab, where one class or another is the question.
     */
    int armorClass{kOwnClass};
    /** Empty shows every item type. */
    std::string type;
    char search[kSearchCapacity]{};

    /** @return Number of filters narrowing the results beyond the defaults. */
    [[nodiscard]] int narrowing() const noexcept {
        const bool armor = category == Category::armor;
        return (rarity != 0 ? 1 : 0) + (!classOnly || armor ? 0 : 1) + (includeInternal ? 1 : 0)
               + (slot >= 0 ? 1 : 0) + (damageType >= 0 && category == Category::weapons ? 1 : 0)
               + (ammo >= 0 && category == Category::weapons ? 1 : 0)
               + (armorClass != kOwnClass && armor ? 1 : 0);
    }
};

/** Results for one exact set of armory filters, rebuilt only when that set changes. */
struct BrowseResults {
    std::vector<const edit::CatalogItem*> items;
    /** Items each type would show, counted before the type filter so the type picker can offer it. */
    std::map<std::string, std::size_t> types;
    /** Results across every type, which the type picker shows against "All types". */
    std::size_t total{};
    std::string key;
};

/** Values the grant controls carry for an item the character does not own yet. */
struct Grant {
    int power{1050};
    int quantity{1};
};

/** Perk picker state while it is open over one socket lane. */
struct SocketPicker {
    std::size_t lane{};
    /** Opens one step past the native pool: the socket's type across the item's subtype. */
    edit::PlugScope scope{edit::PlugScope::socketAndSubtype};
    std::vector<std::uint16_t> options;
    char search[kSearchCapacity]{};
    /** Filters over the offered plugs: a plug type or empty for all, a tier or zero for all. */
    std::string type;
    int rarity{};
    Sort sort{Sort::name};
    /** Offer the internal and placeholder plugs the catalog carries. */
    bool includeInternal{};
    /** Raised by a socket lane and consumed once the frame's widgets have all been submitted. */
    bool requested{};
};

/** Armor stat targets, kept for one item so switching items reloads them from that item. */
struct StatTargets {
    edit::Stats values{};
    std::uint64_t owner{};
};

/** Randomizer options, which are its slot mask, the power it rolls at, and its generator. */
struct Randomizer {
    std::array<bool, kSlotCount> slots{true, true, true, true, true, true, true, true};
    std::mt19937 engine{std::random_device{}()};
};

/** 64 bytes hold any loadout name a player types. */
inline constexpr std::size_t kLoadoutNameCapacity = 64;

/** Saved loadouts, read from the Dawn folder the first time the page needs them. */
struct Loadouts {
    bool loaded{};
    /**
     * False when a loadouts file is there but not every line of it could be read. Nothing is written
     * over it then, because saving would replace every loadout it holds with only the ones read; the
     * sheet reads it again each time it opens, so a file fixed by hand is taken up then.
     */
    bool writable{true};
    std::vector<edit::SavedLoadout> entries;
    /** Name typed for the next loadout saved. */
    char name[kLoadoutNameCapacity]{};
    /** Loadout the sheet has chosen, whose actions it shows, or -1. */
    int chosen{-1};
    /** Set when the sheet opens, so its first frame chooses the loadout that is on now. */
    bool chooseOnOpen{};
    /** Set when a row was chosen by something other than the pointer, so the list brings it into view. */
    bool reveal{};
    /** Loadout whose Delete was pressed once and now asks to be confirmed, or -1. */
    int pendingDelete{-1};
    /** Loadout whose Save Over was pressed once and now asks to be confirmed, or -1. */
    int pendingSave{-1};
    /** Loadout being renamed in place, or -1, and the name being typed for it. */
    int renaming{-1};
    char rename[kLoadoutNameCapacity]{};
    /** Set when a rename starts, so its field takes the keys on the frame it first appears. */
    bool focusRename{};
};

/**
 * Edits the player can take back and make again. An edit is recorded once it is finished, which for a
 * control the player holds is when it is let go, against the account as it stood before the edit.
 */
struct History {
    /**
     * One step the player can take back: an edit to the account, a change to the saved loadouts, or
     * both, as equipping a loadout that makes pieces again points the loadout at the new copies.
     */
    struct Step {
        edit::EditStep account;
        /** True when the step changed the saved loadouts, which the two lists then hold whole. */
        bool loadouts{};
        std::vector<edit::SavedLoadout> loadoutsBefore;
        std::vector<edit::SavedLoadout> loadoutsAfter;
    };
    /** The draft as it stood after the last recorded edit, which the next one is measured from. */
    std::unique_ptr<state::AccountState> baseline;
    std::vector<Step> undo;
    std::vector<Step> redo;
    /** Set by an edit and cleared once it has been recorded. */
    bool pending{};
    /**
     * Saved loadouts as they stood before a change an account edit made to them this frame, such as
     * an equip pointing a loadout at the copies it made. The edit's own step takes the change with it.
     */
    bool loadoutsPending{};
    std::vector<edit::SavedLoadout> loadoutsBefore;
};

/**
 * The armor optimizer, which works as Sundial's stat adjuster does: the player sets the least of each
 * stat, and a plan refits every piece's stat plugs, and swaps pieces where that helps, to come closest.
 */
struct Optimizer {
    /** Least of each stat the armor should reach, in `edit::Stats` order; zero leaves a stat free. */
    edit::Stats targets{};
    /** What a plan may do besides refitting the armor on now. */
    edit::ArmorOptions options{};
    /**
     * One plan worked out on a worker. The input and minimums are fixed when it starts, so the worker
     * and the sheet can both read them; the plan is the worker's until `done` is set.
     */
    struct Job {
        std::shared_ptr<const edit::ArmorInput> input;
        edit::Stats targets{};
        std::atomic_bool cancel{false};
        std::atomic_bool done{false};
        edit::ArmorPlan plan;
    };
    /** What plans are worked out from, and a digest of the armor and options it was taken from. */
    std::shared_ptr<const edit::ArmorInput> input;
    std::string inputKey;
    /** The plan last found, with the input and minimums it answers. */
    edit::ArmorPlan plan;
    std::shared_ptr<const edit::ArmorInput> planInput;
    edit::Stats planTargets{};
    /**
     * The plan being worked out, if any. A job whose plan came back unfinished stays here, so it is
     * not started again every frame, and the sheet says no plan meets the minimums.
     */
    std::shared_ptr<Job> job;
    /** The minimums last seen, and when they last changed, so a plan waits for them to settle. */
    edit::Stats seenTargets{};
    double changedAt{};
};

/**
 * Whole editor state, owned by the module and reachable only through `model()`.
 * The draft is the only thing here the game can see, and only once an apply commits it.
 */
struct Model {
    edit::Catalog catalog;
    std::unique_ptr<edit::Draft> draft;

    std::thread loader;
    std::atomic<CatalogPhase> phase{CatalogPhase::idle};
    std::atomic_bool cancelLoad{false};
    std::atomic_uint loadProgress{};
    std::string loadError;

    /** Last outcome shown in the action bar: what an edit or an apply came to. */
    std::string status;
    /**
     * The outcome standing when a sheet opened. It spoke of something done before the sheet, so the
     * sheet does not repeat it as though it were what the sheet's own action came to.
     */
    std::string statusAtSheet;
    /** True when that outcome was a refusal, which the bar sets apart from a result. */
    bool statusFailed{};
    /**
     * What the last edit had to say beyond succeeding, such as what a loadout could not put back.
     * The apply that publishes the edit writes its own outcome over the status, so this leads it.
     */
    std::string editNote;
    /** Set when the running account moved on while the draft held unapplied edits. */
    bool accountDiverged{};
    /** Player setting: commit each edit to the running game as soon as it is made. */
    bool applyInstantly{true};
    /** Raised by an edit and consumed once the frame's widgets have all been submitted. */
    bool applyRequested{};
    /** Tick of the last account divergence check, which is throttled well below frame rate. */
    std::uint64_t lastDivergenceCheckTick{};
    /** While non-zero, the last apply reached no signed-in peer and is re-offered until this tick. */
    std::uint64_t republishUntilTick{};

    View view{View::characters};
    std::size_t character{};
    /** Width of the side inspector, which the player can drag. Sundial's workspace is resizable. */
    float inspectorWidth{};
    Selection selection;
    Browse browse;
    BrowseResults results;
    Grant grant;
    SocketPicker picker;
    /** Owned instance waiting on the removal confirmation, or zero. */
    std::uint64_t removal{};
    /** Raised by a removal action and consumed by the page, the scope that owns the confirmation. */
    bool removalRequested{};
    StatTargets targets;
    Randomizer randomizer;
    Loadouts loadouts;
    History history;
    Optimizer optimizer;
    /** Items picked on the character inventory page for one action on all of them, by instance. */
    std::vector<std::uint64_t> picked;
    /** Power the selection bar sets every selected piece of gear to. */
    int pickPower{1050};

    /**
     * The icon sweep, which reads every package's entry table and so runs on its own thread, and
     * only once the browser is opened. The worker fills the two vectors and then flips the phase;
     * the page moves them into the catalog on its own thread.
     */
    std::thread iconSweeper;
    std::atomic<IconSweepPhase> iconSweep{IconSweepPhase::idle};
    std::vector<edit::IconRow> sweptIcons;
    std::vector<std::string> sweptPackages;
    std::vector<edit::IconOwner> sweptOwners;
    /** Icon browser: the first row on the page, and the edge it draws each tile at. */
    int iconRow{};
    float iconTile{52.0F};
    /** Package the browser is filtered to, as an index into the catalog's, or -1 for all. */
    int iconPackage{-1};
    /** Icons passing the filter, and the filter the list was built for. */
    std::vector<std::uint32_t> iconFiltered;
    int iconFilterBuilt{-2};
    /** Icon the viewer is showing, as an index into `iconFiltered`, or -1 for none. */
    int iconViewed{-1};
    /** Raised by a tile and consumed by the page, which is the scope that owns the viewer. */
    bool iconViewRequested{};
    /** The browser's find field, which takes a tag or a row, and whether the last find missed. */
    char iconFind[kSearchCapacity]{};
    bool iconFindMissed{};
    /** What the viewer's last action came to, such as where an export went, and if it failed. */
    std::string iconNote;
    bool iconNoteFailed{};
    /** Tag of the export the worker is still writing, or zero. */
    std::uint32_t iconExporting{};
    /** Tag waiting on the game window to put it on the clipboard, or zero. */
    std::uint32_t iconCopying{};
    /** The one layer of the viewed icon shown alone, counted as the preview draws them, or -1 for all. */
    int iconLayer{-1};

    /** Inventory page filter: a slot index, or -1 for every slot. */
    int inventorySlot{-1};
    char inventorySearch[kSearchCapacity]{};
    /** Profile page search, kept apart so switching pages does not carry one filter to the other. */
    char profileSearch[kSearchCapacity]{};

    /**
     * `reset` joins both workers before the model goes. Dawn skips that teardown when a hook cannot
     * come off, and a joinable std::thread destroyed at exit calls std::terminate, so a worker still
     * joinable here is detached instead. The loader stays joinable after it finishes.
     */
    ~Model() {
        if (loader.joinable()) {
            loader.detach();
        }
        if (iconSweeper.joinable()) {
            iconSweeper.detach();
        }
    }
};

/** @return The module state. It exists for the whole time the page is registered. */
[[nodiscard]] Model& model() noexcept;

/** @return True while the module state exists, which the frame entry checks first. */
[[nodiscard]] bool live() noexcept;

/** @return The character the pages are editing. Only called once a draft exists. */
[[nodiscard]] state::CharacterState& character() noexcept;

/** @return One character's name as the tabs write it, such as "Hunter 2". */
[[nodiscard]] std::string character_label(std::size_t index) noexcept;

/**
 * Draws a sheet's outcome line: what its last action came to, once something has been done since
 * the sheet opened, in the refused colour when it was refused. The sheet lies over the bar, so it says
 * here what the bar would say.
 * @param forced A standing problem to show in place of the outcome, such as a file that cannot be
 * read, or null.
 */
void draw_sheet_outcome(const char* forced = nullptr) noexcept;

/**
 * @return The character's item with this instance id, or null when it owns none.
 * @param instance Owned instance id.
 */
[[nodiscard]] edit::Item* find_owned_item(std::uint64_t instance) noexcept;

/** @return The selected owned item, or null when the selection names a catalog item only. */
[[nodiscard]] edit::Item* selected_item() noexcept;

/** Points the inspector at one catalog item, and optionally at one instance the character owns. */
void select(const edit::CatalogItem& item, std::uint64_t instance = 0) noexcept;

/** Clears the inspector selection. */
void clear_selection() noexcept;

/**
 * Removes one stowed item from the character's inventory and marks the draft changed.
 * An equipped item is not in that array and is left alone; unequip it first.
 * @param instance Owned instance id.
 * @return True when an item was removed.
 */
bool erase_owned_item(std::uint64_t instance) noexcept;

/**
 * Marks the draft changed and, in instant mode, queues the apply that publishes it.
 * @param publish False for an edit that cannot stand on its own yet, such as a class change
 *        before the matching armor and subclass are equipped. The draft still shows it.
 */
void mark_changed(bool publish = true) noexcept;

/**
 * Records the outcome of one edit call that maintains the draft itself.
 * @param succeeded True when the edit changed the draft.
 */
void record_edit(bool succeeded) noexcept;

/**
 * Records an edit made through a control the player holds, such as a slider or a stepper.
 * The draft follows every frame, and the apply waits until the control is released, so a drag
 * commits once instead of once per frame. Call it immediately after the control.
 * @param changed True when the control reported a change this frame.
 */
void record_scalar_edit(bool changed) noexcept;

/** Rebases the draft onto the running account, discarding any unapplied edits. */
void reload_account() noexcept;

/** Starts, or restarts, the background catalog load. */
void start_catalog_load() noexcept;

/** Draws the whole page inside the active Core UI frame. */
void draw() noexcept;

/** Releases the catalog worker and clears state between UI lifecycles. */
void reset() noexcept;

/** View bodies. Each draws into the region the frame has already sized. */

/** Identity and progression fields, drawn above the equipped loadout on the Characters view. */
void draw_character_fields() noexcept;
/** The equipped armor stat totals, drawn as one row under the loadout heading. */
void draw_armor_totals() noexcept;

/**
 * Draws six stat totals on one line at the cursor, in the order the game lists them: each stat's
 * mark, then its name when `named`, then its value. The Characters page names them; a surface short
 * of room leaves the names under the pointer. Every line of stats in the editor is this one.
 */
void draw_stat_totals(const edit::Stats& totals, bool named) noexcept;

/** The equipped loadout cards, drawn below the identity fields on the Characters view. */
void draw_equipment() noexcept;
void draw_armory_page() noexcept;
void draw_character_inventory_page() noexcept;
void draw_profile_inventory_page() noexcept;

/** Draws the icon container browser, which is how an unnamed icon is found. */
void draw_icons_page() noexcept;

/** The subclass and path pickers, drawn as the second group of the Characters view. */
void draw_subclass_group(float labelWidth, float controlWidth) noexcept;

/** The five ability pickers, drawn as the third group of the Characters view. */
void draw_ability_group(float labelWidth, float controlWidth) noexcept;

/** Draws the item detail pane beside, or instead of, the page body. */
void draw_inspector() noexcept;

/**
 * @return How much of the page's right edge the side workspace lies over, in framebuffer pixels.
 * A row of controls that must stay reachable lays itself out inside the width this leaves.
 */
[[nodiscard]] float overlay_width() noexcept;

/**
 * @return The armor stat targets bound to one owned armor piece.
 * The targets follow the item the inspector shows: a new item reloads them from its own roll, and
 * the tooltip's stat block then draws and moves them in place.
 * @param item Owned armor piece being edited.
 */
[[nodiscard]] edit::Stats& stat_targets_for(const edit::Item& item) noexcept;

/**
 * Rolls the armor to its targets once a bar has been let go of, and settles the targets on what
 * the roll reached.
 * @param item Owned armor piece being edited.
 * @param released True on the frame a stat bar was released.
 */
void settle_stat_targets(edit::Item& item, bool released) noexcept;

/** Draws the perk picker modal when a socket row has opened it. */
void draw_perk_picker() noexcept;

/**
 * Asks to remove one stowed item. Nothing is deleted until the page's confirmation is accepted,
 * whichever action asked: the card's menu and the pane's own button both come through here.
 * @param instance Owned instance id.
 */
void request_removal(std::uint64_t instance) noexcept;

/** Draws the removal confirmation once an action has asked for one. */
void draw_removal_confirm() noexcept;

/**
 * Sends one stowed item from the character being edited to another, and says where it went.
 * @param instance Owned instance id.
 * @param target Index of the character that receives it.
 * @return True when the item was sent.
 */
bool send_item(std::uint64_t instance, std::size_t target) noexcept;

/** Opens the saved loadouts sheet, from the scope that then draws it. */
void open_loadouts() noexcept;

/** Draws the saved loadouts sheet once it has been opened. */
void draw_loadouts_modal() noexcept;

/** Opens the armor optimizer sheet, from the scope that then draws it. */
void open_optimizer() noexcept;

/** Draws the armor optimizer sheet once it has been opened. */
void draw_optimizer_modal() noexcept;

/**
 * Takes the last edit back, or makes the last one taken back again, and publishes the result as any
 * edit is published.
 * @param forward False to undo, true to redo.
 */
void retrace_edit(bool forward) noexcept;

/**
 * @return What the next undo or redo would change, such as "Hunter 1", or empty when there is none.
 * @param forward False for the undo, true for the redo.
 */
[[nodiscard]] std::string history_subject(bool forward) noexcept;

/** Records an edit finished this frame, once no control is still held. Called after every widget. */
void record_history() noexcept;

/**
 * Queues text for the Windows clipboard. Dawn gives Dear ImGui no clipboard of its own, so the game
 * window writes it once the frame's locks are released, and the outcome comes a frame later.
 * @return False when the text is too long, or another copy is still waiting.
 */
[[nodiscard]] bool request_copy(std::string_view text) noexcept;

/**
 * Takes the outcome of the last queued copy once the game window has written it.
 * @param copied Receives whether the clipboard took the text.
 * @return True once for each queued copy; false while it waits.
 */
[[nodiscard]] bool take_copy_result(bool& copied) noexcept;

/**
 * Records a change to the saved loadouts, already written, as a step of its own.
 * @param before The saved loadouts as they stood before the change.
 */
void record_loadouts_change(std::vector<edit::SavedLoadout> before) noexcept;

/**
 * Notes that an account edit made this frame also changed the saved loadouts, so the edit's step
 * takes the change with it and one undo takes back both.
 * @param before The saved loadouts as they stood before the change.
 */
void note_loadouts_change(std::vector<edit::SavedLoadout> before) noexcept;

/** @return True when the character inventory page has this item picked. */
[[nodiscard]] bool is_picked(std::uint64_t instance) noexcept;

/** Picks one item on the character inventory page, or lets it go again. */
void toggle_pick(std::uint64_t instance) noexcept;

/**
 * A search as typed, split into the words matched against an item's name, type and description, and
 * the filters it must also pass. A filter the page does not know is kept as a word, so it matches the
 * way anything else typed would, rather than being dropped without a word. The inventory pages and
 * the armory read their searches through this one reader, so a filter means the same on each.
 */
struct Query {
    std::string words;
    /** Rarity tier asked for, or -1. */
    int tier{-1};
    /** Weapon or armor asked for, when `kinded`. */
    edit::GearKind kind{edit::GearKind::other};
    bool kinded{};
    /** Damage type asked for, when `damageTyped`. */
    edit::DamageType damageType{edit::DamageType::none};
    bool damageTyped{};
    /** Each of these is -1 when not asked for, else 1 for yes and 0 for no. */
    int locked{-1};
    int equipped{-1};
    int postmaster{-1};
    /** Power comparison: 0 for none, else one of < > = and l for at most, g for at least. */
    char comparison{};
    int power{};

    /** @return True when a filter names something only an item a character holds can have. */
    [[nodiscard]] bool held_only() const noexcept {
        return locked >= 0 || equipped >= 0 || postmaster >= 0 || comparison != 0;
    }
};

/** Said under the pointer over every search field `read_query` reads: what it takes besides a name. */
inline constexpr const char* kSearchTip =
    "Name, type or description. Filters: is:exotic, is:legendary, is:weapon, is:armor, is:arc, "
    "is:solar, is:void, is:locked, is:unlocked, is:equipped, is:postmaster, power:>1000.";

/** @return A search read into its words and its filters. The search is already folded to lower case. */
[[nodiscard]] Query read_query(const std::string& search) noexcept;

/**
 * @return True when one item passes a search: its words and every filter.
 * @param item The item a character holds, or null for a catalog entry or a profile stack, which a
 * filter only a held item can pass leaves out.
 * @param equipped True when the character has the item on.
 */
[[nodiscard]] bool passes(const Query& query,
                          const edit::CatalogItem& definition,
                          const edit::Item* item,
                          bool equipped) noexcept;

/**
 * Draws every socket of one owned item as an editable row: the fitted plug's icon, its name, its
 * type and what it does, with the row itself opening the picker for that lane.
 * @param definition Catalog definition of the item being edited.
 * @param item Owned instance whose sockets are shown.
 * @param width Content width in framebuffer pixels.
 */
void draw_perk_editor(const edit::CatalogItem& definition,
                      const edit::Item& item,
                      float width) noexcept;

/** Opens the perk picker over one socket lane of the selected item. */
void open_perk_picker(const edit::CatalogItem& definition, std::size_t lane) noexcept;

} // namespace dawn::core::ui::modules::loadout::internal
