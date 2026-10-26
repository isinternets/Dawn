#pragma once

#include <string_view>

#include "../../../state/activity/destination/definition.h"
#include "../../../state/build_data/activities/activity_catalog.h"
#include "../../../state/build_data/scenarios/definition.h"
namespace dawn::server::runtime::activity::initial_arrival {

/** A reviewed initial policy tied to one public identity and installed content layout. */
struct Profile final {
    std::int16_t activityIndex;
    std::uint32_t publicHash;
    std::uint32_t gameplaySettingsHash;
    std::uint8_t nativeType;
    std::uint8_t destination;
    std::string_view package;
    std::uint32_t scenarioTag;
    std::string_view mapStem;
    std::uint8_t bubbleCount;
    std::uint8_t bubble;
    std::uint32_t bubbleHash;
    std::uint16_t mapBubble;
    std::uint8_t stateCount;
    std::uint32_t spawnHash;
};

enum class Result { not_applicable, content_mismatch, explicit_arrival, applied };

[[nodiscard]] inline std::string_view
package_name(const state::activity::destination::DestinationSelection& selection) noexcept {
    return selection.packageNameLength <= selection.packageName.size()
        ? std::string_view(reinterpret_cast<const char*>(selection.packageName.data()),
                           selection.packageNameLength)
        : std::string_view{};
}

/** Supplies missing initial policy only; raw descriptor fields and identity remain untouched. */
[[nodiscard]] inline Result apply(
    const Profile& profile,
    const state::build_data::activities::Definition& activity,
    const state::build_data::scenarios::Definition& layout,
    state::activity::destination::DestinationSelection& selection) noexcept {
    namespace destination = state::activity::destination;
    namespace scenarios = state::build_data::scenarios;
    if (selection.activityIndex != profile.activityIndex
        || package_name(selection) != profile.package) {
        return Result::not_applicable;
    }
    // An explicit bubble/slice may represent a resume or a custom launch. Do not combine
    // that arrival with this profile's initial spawn, even when its spawn field is absent.
    if (selection.hasArrivalBubbleOverride || selection.hasSliceSetOverride
        || selection.hasSpawnSetOverride
        || destination::usable_spawn_set_hash(selection.hasArrivalBubbleHash,
                                               selection.arrivalBubbleHash)) {
        return Result::explicit_arrival;
    }
    if (activity.index != profile.activityIndex || activity.hash != profile.publicHash
        || activity.gameplaySettingsHash != profile.gameplaySettingsHash
        || activity.nativeType != profile.nativeType || activity.destination != profile.destination
        || activity.name() != profile.package || layout.tag != profile.scenarioTag
        || layout.truncated != 0 || layout.bubbleCount != profile.bubbleCount
        || profile.bubble >= layout.bubbleCount || profile.bubble >= layout.bubbleStates.size()
        || layout.spawnStemLength > layout.spawnStem.size()
        || std::string_view(layout.spawnStem.data(), layout.spawnStemLength) != profile.mapStem
        || layout.bubbleStates[profile.bubble] != scenarios::kBubbleEnabledByte
        || layout.bubbleHashes[profile.bubble] != profile.bubbleHash
        || layout.bubbleMapIndices[profile.bubble] != profile.mapBubble
        || layout.bubbleStateCounts[profile.bubble] != profile.stateCount) {
        return Result::content_mismatch;
    }
    selection.arrivalBubbleOverride = profile.bubble;
    selection.hasArrivalBubbleOverride = true;
    // Let the existing native region encoder derive bubble*8. A forced slice retains
    // its separate established priority and is never introduced by an initial profile.
    if (!destination::usable_spawn_set_hash(selection.hasSpawnSetHash, selection.spawnSetHash)) {
        selection.spawnSetOverride = profile.spawnHash;
        selection.hasSpawnSetOverride = true;
    }
    return Result::applied;
}

} // namespace dawn::server::runtime::activity::initial_arrival
