#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace dawn::core::cellular {

inline void advance_rule_110_weather(std::vector<bool>& cells) {
    auto next = cells;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto left = cells[(index + cells.size() - 1) % cells.size()];
        const auto center = cells[index];
        const auto right = cells[(index + 1) % cells.size()];
        const auto neighborhood = (left << 2) | (center << 1) | right;
        next[index] = ((110U >> neighborhood) & 1U) != 0;
    }
    cells = std::move(next);
}

} // namespace dawn::core::cellular
