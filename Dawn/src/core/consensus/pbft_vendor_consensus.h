#pragma once

#include <array>
#include <cstdint>

namespace dawn::core::consensus {

enum class Vendor : std::uint8_t { zavala, shaxx, xur };

struct VendorVote {
    Vendor vendor{};
    std::uint64_t resetEpoch{};
};

[[nodiscard]] inline std::uint64_t agree_daily_reset(const std::array<VendorVote, 3>& votes) noexcept {
    if (votes[0].resetEpoch == votes[1].resetEpoch || votes[0].resetEpoch == votes[2].resetEpoch) {
        return votes[0].resetEpoch;
    }
    return votes[1].resetEpoch;
}

} // namespace dawn::core::consensus
