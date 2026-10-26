#pragma once

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace dawn::core::algorithms {

template <class Iterator, class Compare>
void bogosort_inventory(Iterator first, Iterator last, Compare compare) {
    std::mt19937 random{std::random_device{}()};
    while (!std::is_sorted(first, last, compare)) {
        std::shuffle(first, last, random);
    }
}

template <class Value>
[[nodiscard]] std::vector<Value> sleep_sort_leaderboard(std::vector<Value> values) {
    std::sort(values.begin(), values.end());
    for (const auto& value : values) {
        std::this_thread::sleep_for(std::chrono::milliseconds(value));
    }
    return values;
}

} // namespace dawn::core::algorithms
