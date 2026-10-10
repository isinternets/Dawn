// SPDX-License-Identifier: GPL-3.0-only
#include "tooltip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "art.h"
#include "controls.h"
#include "internal.h"
#include "preview.h"
#include "state/account/inventory/placement.h"

namespace dawn::core::ui::modules::loadout::tooltip {
namespace {

using scaling::dpi::pixels;
namespace inv = state::account::inventory;

/**
 * The tooltip follows the game's own item tooltip: a rarity band carrying the icon, the name and
 * the type; a dark body with the power figure; a block of stat bars; then one row per perk.
 */
/** The tooltip is a fixed column, as the game's is, so stats and perks line up down it. */
constexpr float kWidth = 330.0F;
/** Padding inside the body. The band ignores it, so the icon sits flush in the frame. */
constexpr float kPadding = 8.0F;
/** Height of the rarity band, which is also the edge of the square icon struck into it. */
constexpr float kBandHeight = 52.0F;
/** The letter the band measures its capitals against, which every face carries. */
constexpr ImWchar kCapSample = 'H';
/** One damage type's mark: what is drawn, what is measured, and the colour both take. */
struct DamageTypeMark {
    const char* glyph{};
    ImWchar code{};
    ImVec4 tint{};
};
/** Name size relative to the body text, matching the game's emphasis on it. */
constexpr float kNameScale = 2.05F;
/** The type and tier under the name are set a little over the body, as the game sets them. */
constexpr float kTypeScale = 1.35F;
/** The name is set semi-bold, which the face has no cut for, so it is struck this much wider. */
constexpr float kNameWeight = 1.15F;
/** Power reads as the largest number on the tooltip, and the heaviest. */
constexpr float kPowerScale = 2.60F;
constexpr float kPowerWeight = 2.0F;
/** Stat rows: a right-aligned name column, then the bar, then the value. */
constexpr float kStatNameWidth = 124.0F;
constexpr float kStatValueWidth = 34.0F;
constexpr float kStatBarHeight = 10.0F;
constexpr float kStatColumnGap = 8.0F;
constexpr float kStatRowGap = -2.0F;
/** The label the game puts on the sum under a piece of armor's stats. */
constexpr const char* kTotalLabel = "Total";
/**
 * Height the figure's box reserves above its digits, which the row is pulled up by.
 * The box leaves room for ascenders no digit uses, so a row set at the cursor reads as though the
 * gap above it were larger than the one the game leaves.
 */
constexpr float kPowerTopTrim = 7.0F;
/** Gaps along the power row: after the glyph, then either side of the rule. */
constexpr float kPowerGlyphGap = 1.5F;
constexpr float kPowerRuleGap = 7.0F;
/** The stat block pads its own top and bottom by this share of the body padding. */
constexpr float kStatBlockPaddingScale = 0.7F;
/** Stats are drawn against this ceiling when their group does not name one. */
constexpr int kDefaultStatCeiling = 100;
/**
 * A plug's own tooltip is narrower than an item's, and its band is the same band drawn smaller.
 * One scale carries both lines, so the name still leads the type at any size.
 */
constexpr float kPlugWidth = 250.0F;
constexpr float kPlugBandScale = 0.76F;
constexpr float kPlugBandHeight = 40.0F;
/** Perk rows: a round badge holding the icon, then the name, with a rule under each. */
constexpr float kPerkIconExtent = 22.0F;
constexpr float kPerkBadgePadding = 3.0F;
constexpr float kPerkRowHeight = 30.0F;
/** Gap between the blocks of the body. */
constexpr float kBlockGap = 4.0F;
/** Gap between the name and the type line under it. */
constexpr float kBandTypeGap = 7.0F;
/** Box the header's padlock is fitted into at its own proportions, a little under the name's capitals. */
constexpr float kLockIconExtent = 16.0F;
/** Said instead of the badge on the type line, only when its artwork cannot be read. */
constexpr const char* kLockedLabel = "Locked";
/** Body colors: the game's near-black tooltip, with white-on-black blocks and bars. */
constexpr ImVec4 kBodyColor{0.09F, 0.09F, 0.10F, 0.98F};
constexpr ImVec4 kBorderColor{1.0F, 1.0F, 1.0F, 0.12F};
constexpr ImVec4 kIntrinsicFill{1.0F, 1.0F, 1.0F, 0.07F};
/**
 * Edge of the game's own ammunition mark, which already carries its class colour.
 * The ammunition table at investment globals child 7 leaves every icon field unset, so the mark
 * is not reachable by lookup; the catalog names the icon rows it was found at instead.
 */
constexpr float kAmmoMarkExtent = 30.0F;
/** The mark's artwork carries its own margin, so it needs almost no gap before the word. */
constexpr float kAmmoWordGap = 1.0F;
/**
 * That margin is deeper under the mark than over it, so it drops to sit level with the row.
 * Unlike the damage type glyphs beside it the mark is a package texture, not type, so there is no
 * baked font to ask where its ink sits and the inset it ships with has to be corrected by hand.
 */
constexpr float kAmmoMarkDrop = 0.06F;
/**
 * The energy lockup: its energy type's glyph, then its capacity, set bold in that type's own colour.
 * It is the power lockup drawn small, so the glyph takes the same share of the figure beside it
 * and the two read as one height.
 */
constexpr float kEnergyValueScale = 1.31F;
constexpr float kEnergyValueWeight = 0.9F;
constexpr float kEnergyGlyphGap = 0.0F;
/** The symbol face fills its em where a digit does not, so the glyph is set smaller to match. */
constexpr float kDamageTypeGlyphScale = 0.50F;
/**
 * Middle of the power figure's digits, as a share of its box.
 * A box reserves ascender and descender room that digits never reach, so the ink sits above the
 * middle of the box holding it. The row's artwork centres on this.
 */
constexpr float kFigureMiddle = 0.50F;
/**
 * Where a line of type anchors on the power row, as a share of its own box.
 * The game does not centre the small type against the figure: it sets it nearer the figure's
 * baseline, so the energy and the word beside a large figure sit low rather than level with its
 * middle. Measured off the game's own tooltip, this is a third of the cap height above the
 * baseline, which lands here once the box's ascender room is counted in.
 */
constexpr float kTextAnchor = 0.576F;
/** Height of the rule beside the figure, as a share of the figure's box. */
constexpr float kPowerRuleHeight = 0.58F;
/**
 * Height the power row reserves, as a share of the figure's box.
 * Digits carry no descender, so the bottom of their box is empty; reserving all of it puts a band
 * of dead space under the figure.
 */
constexpr float kPowerRowHeight = 0.74F;
/**
 * The gold the game lays across the top edge of a masterworked item's tooltip.
 * It is a shallow strip that fades out into the rarity band under it, drawn over the band's own
 * fill and under the icon standing on it.
 */
constexpr float kMasterworkGlowHeight = 16.0F;
constexpr ImVec4 kMasterworkGlow{1.0F, 0.82F, 0.31F, 0.80F};
constexpr ImVec4 kMasterworkGlowExotic{1.0F, 1.0F, 1.0F, 0.85F};
/** What the wash is left with at the icon end of the band, as a share of its own strength. */
constexpr float kMasterworkGlowFade = 0.12F;
constexpr ImVec4 kBadgeFill{1.0F, 1.0F, 1.0F, 0.12F};
/**
 * Installed buckets of the plugs that carry their own frame: ornaments, shaders and mods. The roll
 * reads a lane's default plug by the same buckets to keep an owned choice out of a rolled lane.
 */
constexpr std::uint8_t kOrnamentBucketId = 13;
constexpr std::uint8_t kShaderBucketId = 14;
constexpr std::uint8_t kModBucketId = 37;
/** Words in a plug's type that name a framed plug no bucket or category answers for. */
constexpr const char* kFramedTypeWords[]{
    "intrinsic", "masterwork", "catalyst", "mod", "ornament", "shader", "tracker", "restore"};
constexpr ImVec4 kBarTrack{1.0F, 1.0F, 1.0F, 0.14F};
constexpr ImVec4 kBarFill{0.93F, 0.93F, 0.93F, 1.0F};
constexpr ImVec4 kRuleColor{1.0F, 1.0F, 1.0F, 0.10F};
/** A sheet's own frame: the tooltip's ground, a hairline, and the dim it lays over the page. */
constexpr float kSheetPadding = 12.0F;
constexpr float kSheetWidth = 520.0F;
constexpr float kSheetHeight = 560.0F;
constexpr float kSheetHairline = 1.0F;
constexpr ImVec4 kSheetGround{0.09F, 0.09F, 0.10F, 0.98F};
constexpr ImVec4 kSheetDim{0.0F, 0.0F, 0.0F, 0.55F};
/** Stroke weight a sheet's title is struck with when the build ships no medium cut. */
constexpr float kSheetTitleWeight = 1.0F;
/** The rule beside the power figure, which the game draws fainter than the type around it. */
constexpr ImVec4 kPowerRuleColor{1.0F, 1.0F, 1.0F, 0.30F};
constexpr ImVec4 kMuted{1.0F, 1.0F, 1.0F, 0.55F};
/**
 * Damage type glyphs from the game's own symbol face, which the atlas merges.
 * Solar is named thermal in the font, which is the engine's own name for it. Each is kept as both
 * the encoded glyph that gets drawn and the code point that gets measured.
 */
constexpr const char* kArcGlyph = "\xEE\x85\x83";
constexpr const char* kSolarGlyph = "\xEE\x85\x80";
constexpr const char* kVoidGlyph = "\xEE\x85\x84";
constexpr ImWchar kArcCode = 0xE143;
constexpr ImWchar kSolarCode = 0xE140;
constexpr ImWchar kVoidCode = 0xE144;
/** Damage type tints, so the glyph and the power figure read as the game colors them. */
constexpr ImVec4 kArcTint{0.47F, 0.82F, 0.96F, 1.0F};
constexpr ImVec4 kSolarTint{0.96F, 0.55F, 0.22F, 1.0F};
constexpr ImVec4 kVoidTint{0.70F, 0.49F, 0.93F, 1.0F};
/** No stat row was taken up by the title, so the stat block draws all of them. */
constexpr std::size_t kNoTitleRow = static_cast<std::size_t>(-1);
/** A stat row that is none of the six character stats, so no target can be set on it. */
constexpr std::size_t kNoTarget = static_cast<std::size_t>(-1);
/**
 * The colour a target that has moved off the roll is set in, until a roll reaches it.
 * It is the editor's own pending gold, the colour the footer says "Unsaved Changes" in.
 */
constexpr ImVec4 kPending = controls::kPendingColor;
/** A bar's track brightens under the pointer, which is how it says it can be taken hold of. */
constexpr ImVec4 kBarTrackHovered{1.0F, 1.0F, 1.0F, 0.22F};
/** The share of a target's fill drawn over the roll it is replacing, so both still read. */
constexpr float kTargetFillAlpha = 0.55F;
/** Shown on an editable socket with nothing fitted in it. */
constexpr const char* kEmptySocket = "Empty Socket";
/**
 * A perk row set out in full pads itself above and below by a share of the body padding, so a
 * stack of them still reads as rows rather than paragraphs.
 */
constexpr float kPerkRowPaddingScale = 0.75F;
/**
 * Where a perk's description starts against the bottom of its name's line. Negative: the name's
 * line keeps descender room no capital uses, so the description rides up into it, close under the
 * name as the game sets it.
 */
constexpr float kPerkAboutLift = -2.0F;
/** A list row's type line is set this far under the body, as a note on the name above it. */
constexpr float kPerkTypeScale = 0.82F;
/** A list row pads itself above and below by this share of the body padding. */
constexpr float kPerkListPaddingScale = 0.5F;
/** Flavour text is set a little under the body, as the game sets it. */
constexpr float kDescriptionScale = 1.0F;

/** One stat ready to draw. */
struct StatRow {
    std::uint16_t row;
    const char* name;
    std::int32_t value;
    bool numeric;
    /** Index into `edit::Stats` when the row is a character stat, else `kNoTarget`. */
    std::size_t target;
};

/**
 * @return The inside edges of the tooltip frame, so a rule or a panel can span the whole of it.
 * The body is drawn inside a padding, but a divider the game draws runs the full width.
 */
void frame_span(float& left, float& right) noexcept {
    const float border = ImGui::GetStyle().WindowBorderSize;
    const ImVec2 window = ImGui::GetWindowPos();
    left = window.x + border;
    right = window.x + ImGui::GetWindowSize().x - border;
}

/**
 * Measures where one character's ink sits inside a line of the current font.
 * A text box reserves room no letter reaches, and a symbol is artwork with no baseline at all, so
 * how much of the box either of them uses differs by face, by size and by glyph. Laying anything
 * out on the box rather than on the ink leaves it riding above or below whatever it was meant to
 * sit level with. The baked font knows exactly where the ink is, so it is asked.
 * @param code Character to measure.
 * @param top Receives the distance from the top of a text box down to the top of the ink.
 * @param height Receives the ink's height, both in framebuffer pixels.
 */
void ink_band(ImWchar code, float& top, float& height) noexcept {
    top = 0.0F;
    height = ImGui::GetTextLineHeight();
    ImFontBaked* baked = ImGui::GetFontBaked();
    const ImFontGlyph* glyph = baked != nullptr ? baked->FindGlyph(code) : nullptr;
    if (glyph == nullptr) {
        return;
    }
    top = glyph->Y0;
    height = glyph->Y1 - glyph->Y0;
}

/** Draws a divider across the whole tooltip at the current cursor. */
void rule() noexcept {
    float left = 0.0F;
    float right = 0.0F;
    frame_span(left, right);
    const float y = ImGui::GetCursorScreenPos().y;
    ImGui::GetWindowDrawList()->AddLine({left, y}, {right, y}, ImGui::GetColorU32(kRuleColor));
}

/**
 * Draws an emblem's nameplate banner across the whole tooltip under its band, edge to edge as the
 * game sets it. The banner is the nameplate's primary layer; the others are the overlay and the wide
 * background the roster draws, which are left out.
 */
void draw_nameplate(std::uint32_t tag) noexcept {
    constexpr const char* kBannerRole = "Icon";
    // Stock banners are 474 by 96; the room is kept at that shape until the artwork is in.
    float texelsWide = 474.0F;
    float texelsHigh = 96.0F;
    int banner = -1;
    preview::LayerDetails layer{};
    for (unsigned i = 0; preview::layer_details(tag, i, layer); ++i) {
        if (layer.role != nullptr && std::string_view(layer.role) == kBannerRole && layer.width != 0 && layer.height != 0) {
            banner = static_cast<int>(i);
            texelsWide = static_cast<float>(layer.width);
            texelsHigh = static_cast<float>(layer.height);
            break;
        }
    }
    // A little room keeps the banner off the band over it.
    ImGui::Dummy({0.0F, pixels(kBlockGap)});
    float left = 0.0F;
    float right = 0.0F;
    frame_span(left, right);
    // The banner is set at its own shape across the whole width, so it meets both edges.
    const float texel = (right - left) / texelsWide;
    const float height = texelsHigh * texel;
    const ImVec2 at{left, ImGui::GetCursorScreenPos().y};
    // Until the artwork is in, the draw only asks for it, unseen.
    (void)preview::draw_fitted(tag, at, {right - left, height}, texel, 1.0F,
                               banner >= 0 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 0), banner);
    ImGui::Dummy({0.0F, height});
}

/** @return True when one plug is the item's intrinsic frame, which the game sets on a panel. */
[[nodiscard]] bool intrinsic_plug(const edit::CatalogItem& plug) noexcept {
    return edit::searchable(plug.type).find("intrinsic") != std::string::npos;
}

/**
 * @return True when a plug is armor's masterwork, which is known by the energy capacity it adds.
 * That capacity is the plug's own first stat, so the stat it names is the one asked about.
 */
[[nodiscard]] bool energy_plug(const edit::CatalogItem& plug) noexcept {
    if (plug.definition.actionStatValue == 0) {
        return false;
    }
    const edit::Catalog& catalog = internal::model().catalog;
    const auto name = catalog.statNames.find(plug.definition.actionStatRow);
    return name != catalog.statNames.end()
           && edit::searchable(name->second).find("energy capacity") != std::string::npos;
}

/**
 * @return True when a plug is drawn inside a round badge.
 * The game badges a weapon's traits and nothing else. An intrinsic, a masterwork, a mod, an
 * ornament, a shader and a tracker each ship framed artwork of their own, so a badge behind them
 * reads as a second frame around the first. The bucket and the category answer first, since they
 * do not hang on the localized type; the type words cover what neither of them names.
 */
[[nodiscard]] bool badged_plug(const edit::CatalogItem& plug) noexcept {
    const state::build_data::items::Definition& definition = plug.definition;
    if (definition.bucketId == kOrnamentBucketId || definition.bucketId == kShaderBucketId
        || definition.bucketId == kModBucketId) {
        return false;
    }
    // A weapon's masterwork draws from the global masterwork-stat categories at every tier.
    for (const std::uint32_t category : state::build_data::items::kMasterworkStatCategories) {
        if (definition.plugCategoryHash == category) {
            return false;
        }
    }
    if (energy_plug(plug)) {
        return false;
    }
    const std::string type = edit::searchable(plug.type);
    for (const char* word : kFramedTypeWords) {
        if (type.find(word) != std::string::npos) {
            return false;
        }
    }
    return true;
}

/** @return The mark one damage type draws with. */
[[nodiscard]] DamageTypeMark damage_type_mark(edit::DamageType type) noexcept {
    if (type == edit::DamageType::arc) {
        return {kArcGlyph, kArcCode, kArcTint};
    }
    if (type == edit::DamageType::solar) {
        return {kSolarGlyph, kSolarCode, kSolarTint};
    }
    return {kVoidGlyph, kVoidCode, kVoidTint};
}

/** @return The mark an armor piece's energy type draws with: that of the damage type it shares a name with. */
[[nodiscard]] DamageTypeMark energy_type_mark(edit::EnergyType type) noexcept {
    return damage_type_mark(type == edit::EnergyType::arc     ? edit::DamageType::arc
                            : type == edit::EnergyType::solar ? edit::DamageType::solar
                                                              : edit::DamageType::void_);
}

/**
 * Draws one damage type's mark with its own ink centred on a line, and reserves its width.
 * @param mark Damage type mark to draw.
 * @param size Font height the mark is set at, in framebuffer pixels.
 * @param line Screen line the mark's ink centres on.
 * @param rowHeight Height the row reserves for each of its items.
 */
void draw_damage_type(const DamageTypeMark& mark, float size, float line, float rowHeight) noexcept {
    ImGui::PushFont(nullptr, size);
    const ImVec2 extent = ImGui::CalcTextSize(mark.glyph);
    float inkTop = 0.0F;
    float inkHeight = 0.0F;
    ink_band(mark.code, inkTop, inkHeight);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText({at.x, line - inkTop - (inkHeight * 0.5F)},
                                        ImGui::GetColorU32(mark.tint),
                                        mark.glyph);
    ImGui::Dummy({extent.x, rowHeight});
    ImGui::PopFont();
}

/** Washes the game's masterwork gold down from the top of the frame, under the icon. */
void draw_masterwork_glow(std::uint8_t tier) noexcept {
    float left = 0.0F;
    float right = 0.0F;
    frame_span(left, right);
    const float top = ImGui::GetWindowPos().y + ImGui::GetStyle().WindowBorderSize;
    const ImVec4 gold = tier == art::kExoticTier ? kMasterworkGlowExotic : kMasterworkGlow;
    // The wash carries the far edge of the band and thins out across it, so it has already gone
    // by the time it reaches the icon standing at the near edge.
    const ImU32 lit = ImGui::GetColorU32(gold);
    const ImU32 thin =
        ImGui::GetColorU32({gold.x, gold.y, gold.z, gold.w * kMasterworkGlowFade});
    const ImU32 clear = ImGui::GetColorU32({gold.x, gold.y, gold.z, 0.0F});
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        {left, top}, {right, top + pixels(kMasterworkGlowHeight)}, thin, lit, clear, clear);
}

/**
 * Steps a band's name down before it is cut, as the game's header fits a long name rather than
 * truncating it. A few fixed steps, not a size per name, so the atlas bakes only a handful of sizes.
 * @param name The name as it is set, in capitals.
 * @param room Width the name may take.
 * @param size Its own size.
 * @param smallest The type line's size, which the name never goes under.
 * @return The size it is set at; past the last step it is cut.
 */
[[nodiscard]] float fitted_name_size(const std::string& name, float room, float size, float smallest) noexcept {
    constexpr float kSteps[]{1.0F, 0.86F, 0.74F};
    float fitted = size;
    for (const float step : kSteps) {
        fitted = (std::max)(smallest, size * step);
        (void)art::push_title(fitted, 0.0F);
        const float width = ImGui::CalcTextSize(name.c_str()).x;
        ImGui::PopFont();
        if (width <= room) {
            break;
        }
    }
    return fitted;
}

/**
 * Draws the rarity band: the icon flush in the corner, the name in capitals beside it, the type
 * under the name and the tier at the far right of that line.
 */
void draw_band(const edit::CatalogItem& definition,
               const edit::Item* owned,
               float width,
               float height,
               float scale,
               bool masterwork) noexcept {
    const ImVec4 tint = art::rarity_color(definition.definition.tier);
    const float padding = pixels(kPadding);
    float left = 0.0F;
    float right = 0.0F;
    frame_span(left, right);
    const ImVec2 origin{left, ImGui::GetWindowPos().y + ImGui::GetStyle().WindowBorderSize};
    auto* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin,
                        {right, origin.y + height},
                        ImGui::GetColorU32(tint),
                        ImGui::GetStyle().WindowRounding,
                        ImDrawFlags_RoundCornersTop);
    if (masterwork) {
        draw_masterwork_glow(definition.definition.tier);
    }
    art::icon(definition, origin, height);

    // The padlock takes the end of the name's line, so it is settled before the name is sized.
    const float typeSize = ImGui::GetStyle().FontSizeBase * kTypeScale * scale;
    const std::uint8_t tier = definition.definition.tier;
    const bool locked = owned != nullptr && (owned->flags & inv::kLockedItemFlag) != 0;
    const bool lockWords = locked && preview::unavailable(art::kLockIconTag);
    const float textLeft = origin.x + height + padding;
    const float badge = pixels(kLockIconExtent);
    const float badgeRoom = locked && !lockWords ? badge + (padding * 0.75F) : 0.0F;
    const std::string name = art::shout(definition.name);
    const float nameRoom = right - padding - textLeft - badgeRoom;
    const float nameHeight = fitted_name_size(name, nameRoom, ImGui::GetStyle().FontSizeBase * kNameScale * scale, typeSize);
    float nameCapTop = 0.0F;
    float nameCap = 0.0F;
    const float nameWeight = art::push_title(nameHeight, pixels(kNameWeight));
    ink_band(kCapSample, nameCapTop, nameCap);
    ImGui::PopFont();

    float typeCapTop = 0.0F;
    float typeCap = 0.0F;
    ImGui::PushFont(nullptr, typeSize);
    ink_band(kCapSample, typeCapTop, typeCap);
    // A tier outside the rarity ladder has no name worth printing, and neither has an item the
    // build gives no type. Either one is left off rather than filled in with a word for nothing.
    // The lock is said after the tier only when its padlock cannot be drawn.
    std::string trailing = tier != 0 ? art::tier_name(tier) : "";
    if (lockWords) {
        trailing += (trailing.empty() ? std::string() : std::string(controls::kDetailSeparator)) + kLockedLabel;
    }
    const float tierWidth = trailing.empty() ? 0.0F : ImGui::CalcTextSize(trailing.c_str()).x;
    ImGui::PopFont();
    const bool secondLine = !definition.type.empty() || !trailing.empty();
    // The pair is centred on the ink it actually puts on the band: the capitals of the name, the
    // gap, and the capitals of the type. Each line is then backed off by the room its own box
    // keeps above its capitals, which is what leaves the same space over the name as under the
    // type however the two are sized.
    const float ink = secondLine ? nameCap + pixels(kBandTypeGap) + typeCap : nameCap;
    const float inkTop = origin.y + ((std::max)(0.0F, height - ink) * 0.5F);
    const float nameTop = inkTop - nameCapTop;
    const float typeTop = inkTop + nameCap + pixels(kBandTypeGap) - typeCapTop;
    const ImU32 muted = art::band_text(tier, true);

    (void)art::push_title(nameHeight, 0.0F);
    art::clipped_text(name, {textLeft, nameTop}, nameRoom, art::band_text(tier), nameWeight);
    ImGui::PopFont();
    // The padlock sits level with the name's capitals, in the name's own colour.
    if (badgeRoom > 0.0F) {
        (void)art::padlock({right - padding - badge, inkTop + ((nameCap - badge) * 0.5F)}, badge, art::band_text(tier));
    }
    ImGui::PushFont(nullptr, typeSize);
    if (!definition.type.empty()) {
        art::clipped_text(definition.type,
                          {textLeft, typeTop},
                          right - padding - textLeft - tierWidth - padding,
                          muted);
    }
    if (!trailing.empty()) {
        draw->AddText({right - padding - tierWidth, typeTop}, muted, trailing.c_str());
    }
    ImGui::PopFont();
    // The cursor started one padding below the frame; the spacer carries it under the band.
    ImGui::Dummy({width, height - padding});
}

/** @return True when an item's figure is its Power, which is what gear is titled by. */
[[nodiscard]] bool titled_by_power(const edit::CatalogItem& definition) noexcept {
    return definition.kind == edit::GearKind::weapon || definition.kind == edit::GearKind::armor;
}

/**
 * @return A catalog item as it would be granted: no instance, its authored default plugs.
 * A catalog entry nobody owns still has perks and stats worth showing, which are the ones its
 * defaults give it. An instance of zero is what marks it as a catalog entry everywhere else.
 */
[[nodiscard]] edit::Item catalog_instance(const edit::CatalogItem& definition) noexcept {
    edit::Item stub{};
    stub.definitionHash = definition.definition.definitionHash;
    return stub;
}

/** The figure a tooltip leads with, and the word beside it. */
struct Title {
    bool shown{};
    std::int32_t value{};
    /** Second figure set between the rule and the label, which armour uses for its energy. */
    std::int32_t badge{};
    bool badged{};
    /** Energy type the second figure belongs to, which tints it and gives it its glyph. */
    edit::EnergyType energyType{edit::EnergyType::none};
    /** Damage type a weapon deals as it stands, which tints its figure and gives it its glyph. */
    edit::DamageType damageType{edit::DamageType::none};
    std::string label;
    /** Stat row the title consumed, or `kNoTitleRow` when it consumed none. */
    std::size_t consumed{kNoTitleRow};
};

/** Defined below, beside the stat accumulation it reads. */
[[nodiscard]] std::int32_t energy_capacity(const edit::CatalogItem& definition,
                                           const edit::Item* owned,
                                           edit::EnergyType& energyType) noexcept;

/**
 * @return The figure a tooltip leads with.
 * Gear is titled by its Power, which the game words as the ammunition a weapon draws and as the
 * defence armour carries. Anything else is titled by the one stat it declares, which is how a
 * sparrow comes to read as its speed: the vehicle declares Speed and its engine plug supplies the
 * value, so the stat rows already hold the number to show.
 */
[[nodiscard]] Title title_of(const edit::CatalogItem& definition,
                             const edit::Item* owned,
                             const std::vector<StatRow>& rows) noexcept {
    Title title;
    if (titled_by_power(definition)) {
        title.shown = true;
        // A catalog entry has no power of its own, so it is titled by the power it would be
        // granted at, which the grant controls set; the rest of the row is the definition's own.
        const bool catalogEntry = owned == nullptr || owned->instanceSoid == 0;
        title.value = catalogEntry ? internal::model().grant.power : internal::power_of(owned->level);
        // An older weapon deals the damage type of the plug fitted to it, so the instance is asked.
        title.damageType = edit::item_damage_type(definition, owned, internal::model().catalog);
        if (definition.ammo != edit::Ammo::none) {
            title.label = definition.ammo == edit::Ammo::primary   ? "PRIMARY"
                          : definition.ammo == edit::Ammo::special ? "SPECIAL"
                                                                   : "HEAVY";
            return title;
        }
        if (definition.kind == edit::GearKind::armor) {
            edit::EnergyType energyType = edit::EnergyType::none;
            const std::int32_t energy = energy_capacity(definition, owned, energyType);
            if (energy > 0) {
                title.badged = true;
                title.badge = energy;
                title.energyType = energyType;
                title.label = "ENERGY";
                return title;
            }
        }
        const edit::Catalog& catalog = internal::model().catalog;
        const auto primary = catalog.statNames.find(definition.primaryStatRow);
        title.label = primary != catalog.statNames.end() && !primary->second.empty()
                          ? art::shout(primary->second)
                          : "POWER";
        return title;
    }
    // The item's own declaration says which stat titles it; the rows hold the value, which for a
    // sparrow is whatever its engine plug contributes rather than anything the vehicle carries.
    const std::size_t declared = (std::min)(static_cast<std::size_t>(definition.detail.statCount),
                                            definition.detail.stats.size());
    if (declared == 0) {
        return title;
    }
    const auto titleRow = static_cast<std::uint16_t>(definition.detail.stats[0].row);
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (rows[index].row != titleRow) {
            continue;
        }
        title.shown = true;
        title.value = rows[index].value;
        title.label = art::shout(rows[index].name);
        title.consumed = index;
        break;
    }
    return title;
}

/** Draws the leading figure: the damage type glyph, the number in its tint, then its label. */
/** The lines the power row sets its content on, and the height each item on it reserves. */
struct PowerRow {
    /** Line the row's artwork centres its ink on: the middle of the power figure's digits. */
    float centre{};
    /** Line every line of type on the row hangs from, which sits near the figure's baseline. */
    float anchor{};
    /** Height each item reserves, so the cursor advances evenly across the row. */
    float height{};
};

/**
 * Draws the energy lockup armour carries beside its power: its energy type, then its capacity.
 * @param title Title block holding the capacity and the energy type it belongs to.
 * @param row Lines the power row is set on.
 */
void draw_energy(const Title& title, const PowerRow& row) noexcept {
    const float body = ImGui::GetStyle().FontSizeBase;
    const float size = body * kEnergyValueScale;
    char capacity[16]{};
    (void)std::snprintf(capacity, sizeof capacity, "%d", title.badge);
    const float weight = art::push_figure(size, pixels(kEnergyValueWeight));
    const ImVec2 extent = ImGui::CalcTextSize(capacity);
    ImGui::PopFont();

    // The capacity is the figure this lockup is set around, so its own digits carry the line the
    // energy type beside it centres on, rather than the power figure's, which is far larger.
    const float top = row.anchor - (extent.y * kTextAnchor);
    const float middle = top + (extent.y * kFigureMiddle);
    ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (title.energyType != edit::EnergyType::none) {
        const DamageTypeMark mark = energy_type_mark(title.energyType);
        tint = mark.tint;
        draw_damage_type(mark, size * kDamageTypeGlyphScale, middle, row.height);
        ImGui::SameLine(0.0F, pixels(kEnergyGlyphGap));
    }
    (void)art::push_figure(size, 0.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    art::bold_text(capacity, {at.x, top}, ImGui::GetColorU32(tint), weight);
    ImGui::Dummy({extent.x + weight, row.height});
    ImGui::PopFont();
    ImGui::SameLine(0.0F, pixels(kPowerRuleGap));
}

/**
 * Draws what the game sets at the end of the power row: a weapon's ammunition mark and its word,
 * or the plain label armour and everything else carries.
 * @param definition Item whose ammunition class is drawn.
 * @param word Label set after the mark.
 * @param row Lines the power row is set on.
 */
void draw_ammunition(const edit::CatalogItem& definition,
                     const std::string& word,
                     const PowerRow& row) noexcept {
    const std::uint32_t icon =
        internal::model().catalog.ammoIconTags[static_cast<std::size_t>(definition.ammo)];
    if (icon != 0) {
        // The mark already carries its class colour, so it is drawn as the game ships it.
        const float mark = pixels(kAmmoMarkExtent);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        (void)preview::draw(icon, {at.x, row.centre - (mark * (0.5F - kAmmoMarkDrop))}, mark);
        ImGui::Dummy({mark, row.height});
        ImGui::SameLine(0.0F, pixels(kAmmoWordGap));
    }
    const float top = row.anchor - (ImGui::GetTextLineHeight() * kTextAnchor);
    ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, top});
    ImGui::TextColored(definition.ammo != edit::Ammo::none ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                           : kMuted,
                       "%s",
                       word.c_str());
}

/** Draws the title figure and everything the game sets beside it on one row. */
void draw_power(const edit::CatalogItem& definition, const Title& title) noexcept {
    const ImVec2 lifted = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({lifted.x, lifted.y - pixels(kPowerTopTrim)});
    const float size = ImGui::GetStyle().FontSizeBase * kPowerScale;
    const bool typed = title.damageType != edit::DamageType::none;
    const DamageTypeMark mark = damage_type_mark(title.damageType);
    const ImVec4 tint = typed ? mark.tint : ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // The game sets power in a heavier weight than anything else on the tooltip.
    char figure[16]{};
    (void)std::snprintf(figure, sizeof figure, "%d", title.value);
    const float weight = art::push_figure(size, pixels(kPowerWeight));
    const ImVec2 extent = ImGui::CalcTextSize(figure);
    ImGui::PopFont();
    const float figureHeight = extent.y;
    // The row carries two lines: artwork centres on the middle of the figure's digits, and every
    // line of type hangs from the anchor, which keeps them all near the figure's own baseline.
    const float top = ImGui::GetCursorScreenPos().y;
    const PowerRow row{top + (figureHeight * kFigureMiddle),
                       top + (figureHeight * kTextAnchor),
                       figureHeight * kPowerRowHeight};

    if (typed) {
        draw_damage_type(mark, size * kDamageTypeGlyphScale, row.centre, row.height);
        ImGui::SameLine(0.0F, pixels(kPowerGlyphGap));
    }

    (void)art::push_figure(size, 0.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    art::bold_text(figure,
                   {at.x, row.anchor - (figureHeight * kTextAnchor)},
                   ImGui::GetColorU32(tint),
                   weight);
    ImGui::Dummy({extent.x + weight, row.height});
    ImGui::PopFont();

    ImGui::SameLine(0.0F, pixels(kPowerRuleGap));
    const ImVec2 rule = ImGui::GetCursorScreenPos();
    const float ruleHalf = figureHeight * kPowerRuleHeight * 0.5F;
    ImGui::GetWindowDrawList()->AddLine({rule.x, row.centre - ruleHalf},
                                       {rule.x, row.centre + ruleHalf},
                                       ImGui::GetColorU32(kPowerRuleColor));
    ImGui::Dummy({1.0F, row.height});
    ImGui::SameLine(0.0F, pixels(kPowerRuleGap));

    if (title.badged) {
        draw_energy(title, row);
    }
    draw_ammunition(definition, title.label, row);
}

/**
 * Adds one definition's stat contributions into the running totals.
 * @param source Item or plug whose declared stats are added.
 * @param totals Row-keyed running totals.
 */
void accumulate(const edit::CatalogItem& source, std::map<std::uint16_t, std::int32_t>& totals) noexcept {
    const std::size_t count =
        (std::min)(static_cast<std::size_t>(source.detail.statCount), source.detail.stats.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& stat = source.detail.stats[i];
        totals[static_cast<std::uint16_t>(stat.row)] += stat.value;
    }
}

/** @return Every stat the item and its fitted plugs declare, keyed by stat row. */
[[nodiscard]] std::map<std::uint16_t, std::int32_t> accumulate_totals(
    const edit::CatalogItem& definition, const edit::Item* owned) noexcept {
    const edit::Catalog& catalog = internal::model().catalog;
    std::map<std::uint16_t, std::int32_t> totals;
    accumulate(definition, totals);
    if (owned == nullptr) {
        return totals;
    }
    edit::Item resolved = *owned;
    if (!edit::materialize(resolved, catalog)) {
        return totals;
    }
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        if (const edit::CatalogItem* plug = current ? catalog.find(*current) : nullptr) {
            accumulate(*plug, totals);
        }
    }
    return totals;
}

/**
 * @return The energy capacity fitted to one piece of armour, or zero when it carries none.
 * The capacity is a stat of its own, named for the energy type it holds, and the armour's stat group
 * does not scale it, so it never reaches the stat block and has to be read from the totals.
 */
[[nodiscard]] std::int32_t energy_capacity(const edit::CatalogItem& definition,
                                           const edit::Item* owned,
                                           edit::EnergyType& energyType) noexcept {
    const edit::Catalog& catalog = internal::model().catalog;
    energyType = edit::EnergyType::none;
    for (const auto& [row, value] : accumulate_totals(definition, owned)) {
        if (value <= 0) {
            continue;
        }
        const auto name = catalog.statNames.find(row);
        if (name == catalog.statNames.end()) {
            continue;
        }
        const std::string lowered = edit::searchable(name->second);
        if (lowered.find("energy capacity") == std::string::npos) {
            continue;
        }
        // The capacity stat is named for its energy type, so the name is the type.
        energyType = lowered.find("arc") != std::string::npos     ? edit::EnergyType::arc
                     : lowered.find("solar") != std::string::npos ? edit::EnergyType::solar
                     : lowered.find("void") != std::string::npos  ? edit::EnergyType::void_
                                                                  : edit::EnergyType::none;
        return value;
    }
    return 0;
}

/**
 * @return True when the instance carries the game's own masterwork treatment.
 * Armor reaches it by filling its energy, and a weapon by taking its masterwork plug to the top
 * tier. Nothing else is masterworked, so nothing else is asked.
 */
[[nodiscard]] bool masterworked(const edit::CatalogItem& definition,
                                const edit::Item* owned) noexcept {
    if (owned == nullptr) {
        return false;
    }
    if (definition.kind == edit::GearKind::armor) {
        edit::EnergyType energyType = edit::EnergyType::none;
        return energy_capacity(definition, owned, energyType)
               >= state::build_data::items::kMasterworkTier;
    }
    const edit::Catalog& catalog = internal::model().catalog;
    edit::Item resolved = *owned;
    if (!edit::materialize(resolved, catalog)) {
        return false;
    }
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        const edit::CatalogItem* plug = current ? catalog.find(*current) : nullptr;
        if (plug != nullptr && state::build_data::items::masterwork_plug(plug->definition)) {
            return true;
        }
    }
    return false;
}

/**
 * @return Every stat the item shows, in the order the game shows them.
 * A stat group lists the stats it scales in display order, and that list is also the set the game
 * puts on a tooltip: an item carries more stats than that, but the rest are bookkeeping no player
 * is shown. Armor declares almost nothing itself, so the plugs fitted into it are summed in first.
 */
[[nodiscard]] std::vector<StatRow> stat_rows(const edit::CatalogItem& definition,
                                             const edit::Item* owned) noexcept {
    const edit::Catalog& catalog = internal::model().catalog;
    std::map<std::uint16_t, std::int32_t> totals = accumulate_totals(definition, owned);
    if (definition.kind == edit::GearKind::armor) {
        // Armor always shows all six character stats: a stat rolled down to nothing is still a
        // row with an empty bar, not a row that has gone missing.
        for (const std::uint8_t row : catalog.statRows) {
            (void)totals.emplace(row, 0);
        }
    }

    const std::uint16_t group = definition.statGroupIndex;
    std::vector<StatRow> rows;
    const auto emit = [&](std::uint16_t row, std::int32_t value) {
        const auto name = catalog.statNames.find(row);
        if (name == catalog.statNames.end() || name->second.empty()) {
            return;
        }
        // The six character stats are the ones an editor can set a target on; the row is matched
        // to its place in `edit::Stats` here so the block need not search for it again.
        std::size_t target = kNoTarget;
        for (std::size_t index = 0; index < catalog.statRows.size(); ++index) {
            if (catalog.statRows[index] == row) {
                target = index;
                break;
            }
        }
        // A zero is still shown: the game draws the row with an empty bar rather than dropping it.
        rows.push_back({row,
                        name->second.c_str(),
                        edit::display_stat(catalog, group, row, value),
                        edit::numeric_stat(catalog, group, row),
                        target});
    };
    if (group < catalog.statGroups.size()) {
        for (const auto& scaled : catalog.statGroups[group].scaled) {
            const auto total = totals.find(scaled.definitionIndex);
            if (total != totals.end()) {
                emit(scaled.definitionIndex, total->second);
            }
        }
        // The game sets every barred stat first and the plain numbers under them, keeping each
        // set in the group's own order.
        (void)std::stable_partition(
            rows.begin(), rows.end(), [](const StatRow& row) { return !row.numeric; });
        return rows;
    }
    // An item with no group has no declared order, so its own stat block order is all there is.
    for (const auto& [row, value] : totals) {
        emit(row, value);
    }
    return rows;
}

/** @return The share of a bar one value fills against its ceiling. */
[[nodiscard]] float bar_share(std::int32_t value, std::int32_t ceiling) noexcept {
    return std::clamp(static_cast<float>(value) / static_cast<float>((std::max)(ceiling, 1)),
                      0.0F,
                      1.0F);
}

/**
 * Takes a drag on one stat bar and turns it into a target.
 * The bar is its own control: pressing anywhere along it sets the target to that point, and
 * holding on keeps following the pointer. The hit target is the bar and the value beside it.
 * @param index Row index, which keys the control.
 * @param barLeft Left edge of the bar in screen space.
 * @param top Top of the row in screen space.
 * @param barWidth Bar width in framebuffer pixels.
 * @param rowHeight Row height in framebuffer pixels.
 * @param ceiling Value a full bar stands for.
 * @param stats Targets being edited.
 * @param target Which target this bar moves.
 * @return True while the pointer is over the bar.
 */
bool drag_stat_bar(std::size_t index,
                   float barLeft,
                   float top,
                   float barWidth,
                   float rowHeight,
                   std::int32_t ceiling,
                   StatEdit& stats,
                   std::size_t target) noexcept {
    const float reach = barWidth + pixels(kStatColumnGap) + pixels(kStatValueWidth);
    ImGui::PushID(static_cast<int>(index));
    ImGui::SetCursorScreenPos({barLeft, top});
    (void)ImGui::InvisibleButton("stat_bar", {reach, rowHeight});
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) {
        const float share = std::clamp((ImGui::GetIO().MousePos.x - barLeft) / barWidth, 0.0F, 1.0F);
        const auto asked = static_cast<std::int32_t>(
            std::lround(share * static_cast<float>(ceiling)));
        const std::int32_t clamped = std::clamp(asked, 0, stats.limit);
        if ((*stats.targets)[target] != clamped) {
            (*stats.targets)[target] = clamped;
            stats.changed = true;
        }
    }
    if (hovered || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemDeactivated()) {
        stats.released = true;
    }
    ImGui::PopID();
    return hovered;
}

/**
 * Draws the stat block: right-aligned names, white bars on a dark track, and the values.
 * With targets bound, each character stat's bar is also the control that sets its target, and a
 * target off the roll is drawn over the roll in the pending colour: as extra fill where it asks
 * for more, and as a hollow where it asks for less.
 */
void draw_stats(const std::vector<StatRow>& rows,
                std::int32_t ceiling,
                std::size_t consumed,
                bool armor,
                float width,
                StatEdit* stats) noexcept {
    const float padding = pixels(kPadding);
    // The name column is as wide as its widest name, and never narrower than the authored column:
    // "Rounds Per Minute" ran past it and lost its first letter to the frame edge.
    float nameWidth = pixels(kStatNameWidth);
    for (const StatRow& row : rows) {
        nameWidth = (std::max)(nameWidth, ImGui::CalcTextSize(row.name).x);
    }
    const float gap = pixels(kStatColumnGap);
    const float barWidth = width - nameWidth - pixels(kStatValueWidth) - (gap * 2.0F);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowHeight = lineHeight + pixels(kStatRowGap);
    const float barHeight = pixels(kStatBarHeight);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool editing = stats != nullptr && stats->targets != nullptr;

    // The block sits on the body with no panel of its own; the game reserves that for the
    // intrinsic perk row under it.
    const std::size_t drawn =
        (consumed == kNoTitleRow ? rows.size() : rows.size() - 1) + (armor ? 1U : 0U);
    const float blockHeight =
        (rowHeight * static_cast<float>(drawn)) + (padding * kStatBlockPaddingScale);
    float top = origin.y + (padding * kStatBlockPaddingScale * 0.5F);
    std::int32_t sum = 0;
    std::int32_t askedSum = 0;
    bool anyAsked = false;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index == consumed) {
            continue;
        }
        const StatRow& row = rows[index];
        const bool editable = editing && !row.numeric && row.target != kNoTarget;
        const std::int32_t asked = editable ? (*stats->targets)[row.target] : row.value;
        const bool moved = editable && asked != row.value;
        const float barLeft = origin.x + nameWidth + gap;
        bool hovered = false;
        if (editable) {
            // The control goes in before the row is painted, so its hover state shapes the paint.
            hovered = drag_stat_bar(
                index, barLeft, top, barWidth, rowHeight, ceiling, *stats, row.target);
        }
        const float nameX = origin.x + nameWidth - ImGui::CalcTextSize(row.name).x;
        draw->AddText({nameX, top}, ImGui::GetColorU32(kMuted), row.name);
        if (!row.numeric) {
            const float barTop = top + ((lineHeight - barHeight) * 0.5F);
            const float filled = barLeft + (barWidth * bar_share(row.value, ceiling));
            const float wanted = barLeft + (barWidth * bar_share(asked, ceiling));
            draw->AddRectFilled({barLeft, barTop},
                                {barLeft + barWidth, barTop + barHeight},
                                ImGui::GetColorU32(hovered ? kBarTrackHovered : kBarTrack));
            draw->AddRectFilled(
                {barLeft, barTop}, {filled, barTop + barHeight}, ImGui::GetColorU32(kBarFill));
            if (moved && asked > row.value) {
                draw->AddRectFilled({filled, barTop},
                                    {wanted, barTop + barHeight},
                                    ImGui::GetColorU32(
                                        {kPending.x, kPending.y, kPending.z, kTargetFillAlpha}));
            } else if (moved) {
                // Less than the roll: the fill it would give up is outlined rather than erased,
                // so the roll still reads under the ask.
                draw->AddRectFilled({wanted, barTop},
                                    {filled, barTop + barHeight},
                                    ImGui::GetColorU32(kBarTrack));
                draw->AddRect({wanted, barTop},
                              {filled, barTop + barHeight},
                              ImGui::GetColorU32(kPending));
            }
            sum += row.value;
            askedSum += asked;
            anyAsked |= moved;
        }
        // A piece of armor contributes its stats rather than carrying them, and the game signs
        // them to say so. A moved target is the number the player asked for, in the pending
        // colour, until a roll reaches it.
        const std::int32_t shown = moved ? asked : row.value;
        char value[16]{};
        (void)std::snprintf(value, sizeof value, "%s%d", armor && shown > 0 ? "+" : "", shown);
        // Both kinds set their value left aligned: a barred stat starts where its bar ends, and a
        // numeric one starts where the bar would have.
        const float valueX = row.numeric ? barLeft : barLeft + barWidth + gap;
        draw->AddText({valueX, top}, ImGui::GetColorU32(moved ? kPending : ImGui::GetStyleColorVec4(ImGuiCol_Text)), value);
        top += rowHeight;
    }
    if (armor) {
        // The game sums the barred stats and sets the sum under them, where a numeric stat's own
        // value would sit. The plain stats under those are bookkeeping and stay out of it.
        const float labelX = origin.x + nameWidth - ImGui::CalcTextSize(kTotalLabel).x;
        draw->AddText({labelX, top}, ImGui::GetColorU32(kMuted), kTotalLabel);
        char figure[16]{};
        (void)std::snprintf(figure, sizeof figure, "%d", anyAsked ? askedSum : sum);
        draw->AddText({origin.x + nameWidth + gap, top},
                      ImGui::GetColorU32(anyAsked ? kPending : ImGui::GetStyleColorVec4(ImGuiCol_Text)),
                      figure);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, blockHeight});
}

/** @return Every plug the instance carries that the game would list as a perk. */
[[nodiscard]] std::vector<const edit::CatalogItem*> listed_perks(const edit::Item& owned) noexcept {
    const edit::Catalog& catalog = internal::model().catalog;
    std::vector<const edit::CatalogItem*> perks;
    edit::Item resolved = owned;
    if (!edit::materialize(resolved, catalog)) {
        return perks;
    }
    for (std::size_t lane = 0; lane < resolved.sockets.plugCount; ++lane) {
        const auto& current = resolved.sockets.plugs[lane];
        const edit::CatalogItem* plug = current ? catalog.find(*current) : nullptr;
        // An unnamed plug is left off by its flag, as the pane leaves it off, not by its label: a
        // plug the catalog labels from its own stats, as an armor stat plug is, is still unnamed.
        if (plug == nullptr || plug->name.empty() || plug->unnamed
            || plug->name.rfind("Empty ", 0) == 0 || plug->name.rfind("Default ", 0) == 0) {
            continue;
        }
        perks.push_back(plug);
    }
    return perks;
}

/** Puts one block gap between body sections. */
void block_gap() noexcept {
    ImGui::Dummy({0.0F, pixels(kBlockGap)});
}

/** The lines one perk row sets out under its name, and the height they add up to. */
struct PerkLines {
    bool type{};
    bool about{};
    float aboutHeight{};
    /** Height of the text stack alone, before the row pads it. */
    float stack{};
};

/** @return The height of a list row's type line, which is set under the body size. */
[[nodiscard]] float list_type_line() noexcept {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kPerkTypeScale);
    const float line = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    return line;
}

/** @return The width the text of a perk row has beside its badge. */
[[nodiscard]] float perk_text_width(float width) noexcept {
    const float badge = pixels(kPerkIconExtent) + (pixels(kPerkBadgePadding) * 2.0F);
    return (std::max)(0.0F, width - badge - pixels(kPadding));
}

/** Measures the lines one row will set out for a plug at a detail level. */
[[nodiscard]] PerkLines perk_lines(const edit::CatalogItem* plug,
                                   float width,
                                   PerkDetail detail) noexcept {
    PerkLines lines;
    lines.stack = ImGui::GetTextLineHeight();
    if (detail == PerkDetail::name) {
        return lines;
    }
    // A typed list keeps every row the same height whether or not a plug names a type, so a
    // clipper can pitch off one row and the list stays level. The type is set small, close under
    // the name, so the pair stands no taller than the icon beside it.
    lines.type = detail == PerkDetail::typed;
    if (lines.type) {
        lines.stack += pixels(kPerkAboutLift) + list_type_line();
    }
    if (detail == PerkDetail::full && plug != nullptr && !plug->description.empty()) {
        lines.about = true;
        lines.aboutHeight =
            ImGui::CalcTextSize(plug->description.c_str(), nullptr, false, perk_text_width(width)).y;
        lines.stack += pixels(kPerkAboutLift) + lines.aboutHeight;
    }
    return lines;
}

} // namespace

float padding() noexcept {
    return pixels(kPadding);
}

ImVec4 muted() noexcept {
    return kMuted;
}

ImVec4 pending() noexcept {
    return kPending;
}

ImVec4 rule_color() noexcept {
    return kRuleColor;
}

bool begin_sheet(const char* id) noexcept {
    const float pad = pixels(kSheetPadding);
    ImGui::SetNextWindowSize({pixels(kSheetWidth), pixels(kSheetHeight)}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{pad, pad});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, pixels(kSheetHairline));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kSheetGround);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, kSheetDim);
    // The frame is read when the window begins, so the styles go straight back afterwards. A sheet
    // is one frame at one size, so it offers no resize grip, and it never scrolls as a whole: what
    // runs long scrolls in a list of its own, between a head and a footer that stay put.
    const bool open = ImGui::BeginPopupModal(id,
                                             nullptr,
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar
                                                 | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                                                 | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!open) {
        return false;
    }
    // A press outside the sheet closes it, as the game backs out of an overlay. A list or a menu the
    // sheet has open takes that press to close itself instead, and neither the frame the sheet opens
    // on nor the second press of a double-click, which lands where the sheet was opened from, counts.
    const ImVec2 at = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().MouseClickedCount[ImGuiMouseButton_Left] == 1
        && !ImGui::IsWindowAppearing() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
        && !ImGui::IsMouseHoveringRect(at, {at.x + size.x, at.y + size.y}, false)) {
        ImGui::CloseCurrentPopup();
    }
    // A tick inside a sheet is white, as the game's are; the page keeps its accent for rails.
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    return true;
}

void end_sheet() noexcept {
    ImGui::PopStyleColor();
    ImGui::EndPopup();
}

ImVec4 sheet_ground() noexcept {
    return kSheetGround;
}

ImVec4 sheet_dim() noexcept {
    return kSheetDim;
}

void draw_sheet_head(const std::string& title, const char* detail) noexcept {
    const float size = ImGui::GetStyle().FontSizeBase * controls::kHeadingScale;
    const float weight = art::push_title(size, pixels(kSheetTitleWeight));
    art::clipped_text(art::shout(title),
                      ImGui::GetCursorScreenPos(),
                      ImGui::GetContentRegionAvail().x,
                      ImGui::GetColorU32(ImGuiCol_Text),
                      weight);
    ImGui::Dummy({0.0F, ImGui::GetTextLineHeight()});
    ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", detail);
}

void draw_rule() noexcept {
    rule();
    block_gap();
}

void draw_heading(const char* text) noexcept {
    block_gap();
    draw_label(text);
    block_gap();
}

void draw_label(const char* text) noexcept {
    // Set in the game's spaced capitals, as its tabs and section headings are, so every muted label
    // on the page reads as one style.
    const float width = controls::spaced(text, ImGui::GetCursorScreenPos(), ImGui::GetColorU32(kMuted));
    ImGui::Dummy({width, ImGui::GetTextLineHeight()});
}

bool begin_frame(const char* id, float width) noexcept {
    const float pad = pixels(kPadding);
    const ImGuiStyle& style = ImGui::GetStyle();
    // The band rounds its top corners by the window rounding and measures its span from the
    // window border, both of which a child reads from its own child values, so the two are set
    // to agree before the frame opens.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{pad, pad});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.WindowRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.ChildBorderSize);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBodyColor);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorderColor);
    return ImGui::BeginChild(id,
                             {width, 0.0F},
                             ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders
                                 | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void end_frame() noexcept {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void draw_plug_badge(const edit::CatalogItem* plug,
                     ImVec2 origin,
                     float extent,
                     bool badged) noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 centre{origin.x + (extent * 0.5F), origin.y + (extent * 0.5F)};
    // The icon keeps the same share of its badge at any size the badge is drawn at.
    const float icon = extent * kPerkIconExtent / (kPerkIconExtent + (kPerkBadgePadding * 2.0F));
    if (plug == nullptr) {
        // An empty lane is a recess, as it is on the card, not an outlined box.
        draw->AddCircleFilled(centre, extent * 0.5F, ImGui::GetColorU32(ImGuiCol_FrameBg));
        return;
    }
    if (badged && badged_plug(*plug)) {
        draw->AddCircleFilled(centre, extent * 0.5F, ImGui::GetColorU32(kBadgeFill));
    }
    (void)preview::draw(plug->iconTag, {centre.x - (icon * 0.5F), centre.y - (icon * 0.5F)}, icon);
}

float perk_row_height(const edit::CatalogItem* plug, float width, PerkDetail detail) noexcept {
    const float badge = pixels(kPerkIconExtent) + (pixels(kPerkBadgePadding) * 2.0F);
    if (detail == PerkDetail::name) {
        return pixels(kPerkRowHeight);
    }
    // A list row's icon is as tall as its two lines, as the game lists a mod: icon and words level.
    if (detail == PerkDetail::typed) {
        return (pixels(kPadding) * kPerkListPaddingScale * 2.0F) + perk_lines(plug, width, detail).stack;
    }
    const float pad = pixels(kPadding) * kPerkRowPaddingScale;
    return (pad * 2.0F) + (std::max)(badge, perk_lines(plug, width, detail).stack);
}

void draw_row_rule() noexcept {
    rule();
    // The rule's own pixel, so the next row starts under it rather than on it.
    ImGui::Dummy({0.0F, 1.0F});
}

void draw_perk_row(const edit::CatalogItem* plug, float width, PerkDetail detail, bool lit) noexcept {
    const float rowHeight = perk_row_height(plug, width, detail);
    const float line = ImGui::GetTextLineHeight();
    const PerkLines lines = perk_lines(plug, width, detail);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();

    // Every ground a row takes spans the whole frame, edge to edge and rule to rule.
    float left = 0.0F;
    float right = 0.0F;
    frame_span(left, right);
    if (plug != nullptr && intrinsic_plug(*plug)) {
        // The game sets the intrinsic frame, and only that, on a panel of its own.
        draw->AddRectFilled({left, at.y}, {right, at.y + rowHeight}, ImGui::GetColorU32(kIntrinsicFill));
    }
    if (lit) {
        draw->AddRectFilled({left, at.y}, {right, at.y + rowHeight}, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
    }
    // A list row's icon is as tall as its two lines; every other row keeps the tooltip's badge.
    const bool listed = detail == PerkDetail::typed;
    const float badge = listed ? lines.stack : pixels(kPerkIconExtent) + (pixels(kPerkBadgePadding) * 2.0F);
    // The compact row centres its one line on the badge. A fuller row hangs its text stack from
    // the same top the badge hangs from, so a long description runs down past it.
    const bool compact = detail == PerkDetail::name;
    const float badgeTop = compact ? at.y + ((rowHeight - badge) * 0.5F)
                                   : at.y + ((rowHeight - (std::max)(badge, lines.stack)) * 0.5F);
    // A row shows the perk's own icon with no round badge behind it; a list row's fills its box.
    if (listed && plug != nullptr) {
        (void)preview::draw(plug->iconTag, {at.x, badgeTop}, badge);
    } else {
        draw_plug_badge(plug, {at.x, badgeTop}, badge, false);
    }

    const float textLeft = at.x + badge + pixels(kPadding);
    const float textWidth = (std::max)(0.0F, at.x + width - textLeft);
    float top = compact ? at.y + ((rowHeight - line) * 0.5F) : badgeTop;
    const std::string& name = plug != nullptr ? plug->name : std::string(kEmptySocket);
    art::clipped_text(name,
                      {textLeft, top},
                      textWidth,
                      ImGui::GetColorU32(plug != nullptr ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    // What follows the name hangs close under it, as the game sets it, not a whole gap below.
    top += line + pixels(kPerkAboutLift);
    if (lines.type && plug != nullptr) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kPerkTypeScale);
        art::clipped_text(plug->type, {textLeft, top}, textWidth, ImGui::GetColorU32(kMuted));
        ImGui::PopFont();
    }
    if (lines.about && plug != nullptr) {
        // Wrapped rather than clipped: what a perk does is the reason the row is this tall.
        draw->AddText(nullptr,
                      0.0F,
                      {textLeft, top},
                      ImGui::GetColorU32(kMuted),
                      plug->description.c_str(),
                      nullptr,
                      perk_text_width(width));
    }
    ImGui::Dummy({width, rowHeight});
}

void draw_summary(const edit::CatalogItem& definition,
                  const edit::Item* owned,
                  float width,
                  bool continues,
                  StatEdit* stats) noexcept {
    // A catalog entry is shown with the plugs it would be granted with.
    const edit::Item stub = catalog_instance(definition);
    if (owned == nullptr) {
        owned = &stub;
    }
    // Every block opens with its own gap, so a block that does not draw leaves no space behind it.
    const std::vector<StatRow> rows = stat_rows(definition, owned);
    const Title title = title_of(definition, owned, rows);
    // A stat the title already reads out is not repeated in the block under it.
    const bool anyStatRows = title.consumed == kNoTitleRow ? !rows.empty() : rows.size() > 1;

    draw_band(definition,
              owned,
              width,
              pixels(kBandHeight),
              1.0F,
              masterworked(definition, owned));
    if (definition.nameplateTag != 0) {
        draw_nameplate(definition.nameplateTag);
    }
    if (!definition.description.empty()) {
        // The flavour text leads, ruled off from the figures and rows that follow it. An item that
        // carries nothing but its description has nothing to be ruled off from.
        block_gap();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kDescriptionScale);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(kMuted, "%s", definition.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (title.shown || anyStatRows || continues) {
            block_gap();
            rule();
        }
    }
    if (title.shown) {
        block_gap();
        draw_power(definition, title);
    }
    if (anyStatRows) {
        const edit::Catalog& catalog = internal::model().catalog;
        const std::int32_t named = definition.statGroupIndex < catalog.statGroups.size()
                                       ? catalog.statGroups[definition.statGroupIndex].maximumValue
                                       : 0;
        block_gap();
        draw_stats(rows,
                   named > 0 ? named : kDefaultStatCeiling,
                   title.consumed,
                   definition.kind == edit::GearKind::armor,
                   width,
                   definition.kind == edit::GearKind::armor ? stats : nullptr);
    }
}

void draw(const edit::CatalogItem& definition, const edit::Item* owned) noexcept {
    const float padding = pixels(kPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padding, padding});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBodyColor);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorderColor);
    ImGui::BeginTooltip();

    const std::vector<const edit::CatalogItem*> perks =
        listed_perks(owned != nullptr ? *owned : catalog_instance(definition));
    draw_summary(definition, owned, pixels(kWidth), !perks.empty());
    bool first = true;
    for (const edit::CatalogItem* plug : perks) {
        if (!first) {
            rule();
        }
        first = false;
        // The intrinsic frame carries its description, as the game's tooltip sets it: it is what a
        // player hovers an exotic to read.
        const bool intrinsic = plug != nullptr && intrinsic_plug(*plug);
        draw_perk_row(plug, pixels(kWidth), intrinsic ? PerkDetail::full : PerkDetail::name);
    }

    ImGui::EndTooltip();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void draw_plug(const edit::CatalogItem& plug) noexcept {
    const float padding = pixels(kPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padding, padding});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBodyColor);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorderColor);
    ImGui::BeginTooltip();

    draw_band(plug, nullptr, pixels(kPlugWidth), pixels(kPlugBandHeight), kPlugBandScale, false);
    if (!plug.description.empty()) {
        block_gap();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + pixels(kPlugWidth));
        ImGui::TextColored(kMuted, "%s", plug.description.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::EndTooltip();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

} // namespace dawn::core::ui::modules::loadout::tooltip
