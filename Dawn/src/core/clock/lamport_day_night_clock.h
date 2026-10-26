#pragma once

#include <algorithm>
#include <cstdint>

namespace dawn::core::clock {

class LamportDayNightClock {
public:
    [[nodiscard]] std::uint64_t tick() noexcept { return ++counter_; }
    [[nodiscard]] std::uint64_t observe(std::uint64_t remote) noexcept {
        counter_ = std::max(counter_, remote) + 1;
        return counter_;
    }
    [[nodiscard]] bool is_day() const noexcept { return (counter_ / 600) % 2 == 0; }

private:
    std::uint64_t counter_{};
};

} // namespace dawn::core::clock
