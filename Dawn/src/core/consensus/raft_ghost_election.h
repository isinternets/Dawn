#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace dawn::core::consensus {

struct GhostCandidate {
    std::uint64_t id{};
    std::uint64_t term{};
};

[[nodiscard]] inline GhostCandidate elect_ghost_leader(std::span<const GhostCandidate> ghosts) noexcept {
    if (ghosts.empty()) {
        return {};
    }
    return *std::max_element(ghosts.begin(), ghosts.end(), [](const auto& left, const auto& right) {
        return left.term != right.term ? left.term < right.term : left.id < right.id;
    });
}

} // namespace dawn::core::consensus
