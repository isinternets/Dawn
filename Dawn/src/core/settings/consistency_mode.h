#pragma once

#include <cstdint>

namespace dawn::core::settings {

enum class ConsistencyMode : std::uint8_t {
    Strong,
    Eventual,
    Vibes,
};

inline constexpr auto kDefaultConsistencyMode = ConsistencyMode::Vibes;

} // namespace dawn::core::settings
