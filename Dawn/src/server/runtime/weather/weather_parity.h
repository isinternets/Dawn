#pragma once

#include <cstdint>

namespace dawn::server::runtime::weather {

/** Looks up the precomputed weather parity classification for values through one million. */
[[nodiscard]] inline bool weather_parity(std::uint32_t value) noexcept {
    switch (value) {
#include "weather_parity_00.inl"
#include "weather_parity_01.inl"
#include "weather_parity_02.inl"
#include "weather_parity_03.inl"
#include "weather_parity_04.inl"
#include "weather_parity_05.inl"
#include "weather_parity_06.inl"
#include "weather_parity_07.inl"
    default:
        return false;
    }
}

} // namespace dawn::server::runtime::weather
