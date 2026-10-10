// SPDX-License-Identifier: GPL-3.0-only
#include "art.h"
#include "controls.h"

#include "../../fonts/runtime/ui_runtime_font_lifecycle.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "internal.h"
#include "tooltip.h"
#include "preview.h"
#include "state/account/inventory/placement.h"

namespace dawn::core::ui::modules::loadout::art {
namespace {

namespace inv = state::account::inventory;
using scaling::dpi::pixels;

/**
 * Rarity tints, indexed by tier. Tier 5 is Exotic and tier 0 is unclassified.
 * These are the game's own item header colors, so a band here reads as the band in game.
 */
constexpr ImVec4 kRarityTints[]{
    {0.765F, 0.737F, 0.706F, 1.0F},
    {0.765F, 0.737F, 0.706F, 1.0F},
    {0.212F, 0.435F, 0.259F, 1.0F},
    {0.314F, 0.463F, 0.639F, 1.0F},
    {0.322F, 0.184F, 0.396F, 1.0F},
    {0.808F, 0.682F, 0.200F, 1.0F},
};
/** Common and Exotic bands are light, so the game sets dark text on them. */
constexpr ImVec4 kDarkBandText{0.08F, 0.08F, 0.09F, 1.0F};
constexpr ImVec4 kLightBandText{1.0F, 1.0F, 1.0F, 1.0F};
/** Muted band text keeps this share of the text color. */
constexpr float kMutedBandTextOpacity = 0.72F;
/** Tiers at or below Common, and Exotic, take the dark text. */
constexpr std::uint8_t kLastLightBandTier = 1;
/** Tier names in the same order as the tints. */
constexpr const char* kTierNames[]{
    "Unclassified", "Common", "Uncommon", "Rare", "Legendary", "Exotic"};
/** Class names in stable authored order. */
constexpr const char* kClassNames[]{"Titan", "Hunter", "Warlock"};

/** The icon backdrop is the rarity tint at this weight, which keeps art legible on it. */
constexpr float kBackdropWeight = 0.17F;
/** 90 authored pixels is the smallest icon that fits a whole placeholder line. */
constexpr float kPlaceholderTextMinimumExtent = 90.0F;
/** One extra strike per this many pixels of weight, which keeps a stroke solid with no blur. */
constexpr float kBoldStrikeStep = 0.6F;
/** 10 authored pixels of slack keep an ellipsis inside its measured column. */
constexpr float kClipSlack = 10.0F;
/** UTF-8 continuation bytes carry this tag, so a truncation steps back over them. */
constexpr unsigned char kContinuationMask = 0xC0U;
constexpr unsigned char kContinuationTag = 0x80U;

/**
 * Item band geometry, which inventory cards and armory rows share.
 * The band is as tall as the icon it carries, because the icon sits flush inside it. 38 authored
 * pixels is the shortest band that still clears two lines of text, and it keeps the icon plainly
 * smaller than the one the tooltip sets.
 */
constexpr float kBandHeight = 38.0F;
/** Stroke weight the band's name is struck with when the build ships no medium cut. */
constexpr float kBandNameWeight = 0.8F;
/** Negative: the title face keeps descender room no capital uses, so the type line rides up into it. */
constexpr float kBandLineGap = -2.0F;
/** Gap between the icon and the name column beside it, and the room the text keeps at the right. */
constexpr float kBandTextGap = 7.0F;
constexpr float kBandTextInset = 6.0F;
/** Box the padlock of a locked item is fitted into on a band, level with the name. */
constexpr float kBandLockExtent = 14.0F;
/** A band under the pointer lightens by this much. */
constexpr ImVec4 kBandHoverWash{1.0F, 1.0F, 1.0F, 0.10F};

/** @return The tint scaled down to the backdrop weight, at full opacity. */
[[nodiscard]] ImU32 backdrop_color(const ImVec4& tint) noexcept {
    return ImGui::GetColorU32(
        {tint.x * kBackdropWeight, tint.y * kBackdropWeight, tint.z * kBackdropWeight, 1.0F});
}

/**
 * Says what an icon the package reader has not produced is doing, where there is room for a word.
 * A small icon keeps only its tinted backdrop: a letter there read as a button.
 */
void draw_icon_placeholder(const edit::CatalogItem& item, ImVec2 origin, float extent) noexcept {
    if (extent < pixels(kPlaceholderTextMinimumExtent)) {
        return;
    }
    const bool loading = item.iconTag != 0 && !preview::unavailable(item.iconTag);
    const char* text = loading ? "Loading" : "No image";
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImGui::GetWindowDrawList()->AddText({origin.x + ((extent - size.x) * 0.5F), origin.y + ((extent - size.y) * 0.5F)},
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                        text);
}

/**
 * Shows the game-style tooltip for a hovered card.
 * @param item Catalog definition behind the card.
 * @param instance Owned instance whose power and perks are shown, or zero for a catalog entry.
 */
void draw_card_tooltip(const edit::CatalogItem& item, std::uint64_t instance) noexcept {
    const edit::Item* owned = nullptr;
    if (instance != 0) {
        const edit::Item* selected = internal::selected_item();
        owned = selected != nullptr && selected->instanceSoid == instance ? selected : nullptr;
        if (owned == nullptr) {
            owned = internal::find_owned_item(instance);
        }
    }
    tooltip::draw(item, owned);
}

} // namespace

ImVec4 rarity_color(std::uint8_t tier) noexcept {
    return kRarityTints[(std::min)(static_cast<std::size_t>(tier), std::size(kRarityTints) - 1)];
}

ImU32 band_text(std::uint8_t tier, bool muted) noexcept {
    ImVec4 color = tier <= kLastLightBandTier || tier == kExoticTier ? kDarkBandText : kLightBandText;
    if (muted) {
        color.w = kMutedBandTextOpacity;
    }
    return ImGui::GetColorU32(color);
}

const char* tier_name(std::uint8_t tier) noexcept {
    return kTierNames[(std::min)(static_cast<std::size_t>(tier), std::size(kTierNames) - 1)];
}

float collection_card_height() noexcept {
    return std::floor(band_height());
}

float band_height() noexcept {
    return pixels(kBandHeight);
}

std::string shout(const std::string& value) noexcept {
    std::string result = value;
    // Names come from the localized bank, and a byte-wise toupper on a UTF-8 sequence is not safe, so
    // ASCII is folded and the accented Latin-1 letters by hand, as the é of Dragée is. Their capitals
    // sit 0x20 below them in the same two-byte sequence; ÷ sits among them and is no letter.
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto byte = static_cast<unsigned char>(result[i]);
        if (byte < 0x80U) {
            result[i] = static_cast<char>(std::toupper(byte));
        } else if (byte == 0xC3U && i + 1 < result.size()) {
            const auto low = static_cast<unsigned char>(result[++i]);
            if (low >= 0xA0U && low <= 0xBEU && low != 0xB7U) result[i] = static_cast<char>(low - 0x20U);
        }
    }
    return result;
}

const char* class_name(state::CharacterClass value) noexcept {
    return kClassNames[(std::min)(static_cast<std::size_t>(value), std::size(kClassNames) - 1)];
}

void icon(const edit::CatalogItem& item, ImVec2 origin, float extent, bool framed) noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec4 tint = rarity_color(item.definition.tier);
    const ImVec2 corner{origin.x + extent, origin.y + extent};
    draw->AddRectFilled(origin, corner, backdrop_color(tint), pixels(controls::kCardRounding));
    if (!preview::draw(item.iconTag, origin, extent)) {
        draw_icon_placeholder(item, origin, extent);
    }
    if (framed) {
        draw->AddRect(origin, corner, ImGui::GetColorU32(tint), pixels(controls::kCardRounding));
    }
}

bool padlock(ImVec2 origin, float extent, ImU32 tint) noexcept {
    return preview::draw_fitted(kLockIconTag, origin, {extent, extent}, pixels(1.0F), 1.0F, tint);
}

/**
 * Pushes one heavier cut, or the regular face when the build ships none.
 * @return The weight the caller is still left to strike itself.
 */
[[nodiscard]] float push_weight(fonts::runtime::Weight requested,
                                float size,
                                float fallbackWeight) noexcept {
    ImFont* face = fonts::runtime::weight(requested);
    ImGui::PushFont(face, size);
    return face != nullptr ? 0.0F : fallbackWeight;
}

float push_title(float size, float fallbackWeight) noexcept {
    return push_weight(fonts::runtime::Weight::medium, size, fallbackWeight);
}

float push_figure(float size, float fallbackWeight) noexcept {
    return push_weight(fonts::runtime::Weight::displayBold, size, fallbackWeight);
}

void bold_text(const char* text, ImVec2 at, ImU32 color, float weight) noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    if (weight <= 0.0F) {
        draw->AddText(at, color, text);
        return;
    }
    // Striking only to the right grew every glyph off its right edge alone: the upright of a digit
    // thickened while its bowl stayed thin, and the ink it gained came out of the gap before the
    // next figure, which read as a line of badly spaced numbers. The strikes are laid in a ring
    // around the glyph instead, so it thickens evenly and its own centre stays where it was.
    // The ring is centred half a weight in, which leaves the line occupying the same box a caller
    // reserved for it, rather than bleeding a half weight past its left edge.
    const float radius = weight * 0.5F;
    const ImVec2 centre{at.x + radius, at.y};
    const int strikes = (std::max)(1, static_cast<int>(radius / kBoldStrikeStep));
    for (int ring = 1; ring <= strikes; ++ring) {
        const float offset = radius * static_cast<float>(ring) / static_cast<float>(strikes);
        draw->AddText({centre.x - offset, centre.y}, color, text);
        draw->AddText({centre.x + offset, centre.y}, color, text);
        draw->AddText({centre.x, centre.y - offset}, color, text);
        draw->AddText({centre.x, centre.y + offset}, color, text);
    }
    draw->AddText(centre, color, text);
}

void clipped_text(const std::string& text,
                  ImVec2 at,
                  float width,
                  ImU32 color,
                  float weight) noexcept {
    std::string value = text;
    const float limit = width - pixels(kClipSlack);
    bool shortened = false;
    while (!value.empty() && ImGui::CalcTextSize(value.c_str()).x > limit) {
        std::size_t end = value.size() - 1;
        while (end != 0 && (static_cast<unsigned char>(value[end]) & kContinuationMask) == kContinuationTag) {
            --end;
        }
        value.resize(end);
        shortened = true;
    }
    if (shortened) {
        value += "...";
    }
    bold_text(value.c_str(), at, color, weight);
}

void item_band(const edit::CatalogItem* definition, const char* label, ImVec2 origin, float right, bool lit, bool locked) noexcept {
    auto* draw = ImGui::GetWindowDrawList();
    const float band = band_height();
    const ImVec2 corner{right, origin.y + band};
    const ImVec4 tint = definition != nullptr ? rarity_color(definition->definition.tier)
                                              : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    draw->AddRectFilled(origin, corner, ImGui::GetColorU32(tint));
    if (lit) {
        draw->AddRectFilled(origin, corner, ImGui::GetColorU32(kBandHoverWash));
    }
    if (definition != nullptr) {
        icon(*definition, origin, band);
    }

    // The name and its label are centred as a pair against the icon beside them. The name is set
    // as the tooltip sets it, in capitals in the title cut, so the band and the tooltip agree.
    const float textLeft = origin.x + band + pixels(kBandTextGap);
    const float textWidth = right - pixels(kBandTextInset) - textLeft;
    // A locked item's padlock takes the end of the name's line, as the tooltip's header sets it.
    const float lock = pixels(kBandLockExtent);
    const float nameWidth = locked && definition != nullptr ? textWidth - lock - pixels(kBandTextInset) : textWidth;
    const float line = ImGui::GetTextLineHeight();
    const float nameSize = ImGui::GetStyle().FontSizeBase * controls::kHeadingScale;
    const float nameWeight = push_title(nameSize, pixels(kBandNameWeight));
    const float nameLine = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    const std::uint8_t tier = definition != nullptr ? definition->definition.tier : 0;
    // The second line is the item's type, then the label after it. An empty slot has only the label.
    std::string detail = definition != nullptr ? definition->type : std::string();
    // A label named the same as the type, as "Ghost" or "Ship" is, would only say it twice.
    if (label != nullptr && detail != label) {
        detail += detail.empty() ? std::string(label) : controls::kDetailSeparator + std::string(label);
    }
    const float gap = definition != nullptr && !detail.empty() ? pixels(kBandLineGap) : 0.0F;
    const float stack = (definition != nullptr ? nameLine : 0.0F) + (detail.empty() ? 0.0F : line);
    float top = origin.y + ((band - stack - gap) * 0.5F);
    if (definition != nullptr) {
        (void)push_title(nameSize, 0.0F);
        clipped_text(shout(definition->name), {textLeft, top}, nameWidth, band_text(tier), nameWeight);
        ImGui::PopFont();
        if (locked) {
            (void)padlock({right - pixels(kBandTextInset) - lock, top + ((nameLine - lock) * 0.5F)}, lock, band_text(tier));
        }
        top += nameLine + gap;
    }
    if (!detail.empty()) {
        // An empty slot's band is the frame's own dark ground rather than a rarity's, so it takes the
        // muted light text: the dark type a pale band takes all but vanished on it.
        const ImU32 color = definition != nullptr ? band_text(tier, true) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        clipped_text(detail, {textLeft, top}, textWidth, color);
    }
}

void collection_card(const edit::CatalogItem& item, float width) noexcept {
    const float height = band_height();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(static_cast<int>(item.definition.definitionIndex));
    const bool clicked = ImGui::InvisibleButton("collection_item", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = internal::model().selection.holds(item.definition.definitionHash, 0);

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner{origin.x + width, origin.y + height};
    // The row is the band an inventory card leads with, labelled with the slot the item fills.
    item_band(&item, item.slot < inv::kEquipmentSlotCount ? edit::kSlots[item.slot] : nullptr, origin, corner.x, hovered);

    if (selected) {
        draw->AddRect(origin,
                      corner,
                      ImGui::GetColorU32(ImGuiCol_Text),
                      0.0F,
                      0,
                      pixels(controls::kRailWidth));
    }
    if (hovered) {
        draw_card_tooltip(item, 0);
    }
    if (clicked) {
        internal::select(item);
    }
    ImGui::PopID();
}

} // namespace dawn::core::ui::modules::loadout::art
