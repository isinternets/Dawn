#pragma once

#include <cstdint>

namespace dawn::server::runtime::weather {

/** Environments whose patrol weather has bespoke gameplay behavior. */
enum class Environment : std::uint8_t {
    unknown,
    cosmodrome,
    mars,
    tower,
};

/** Precipitation selected for the current simulation tick. */
enum class Precipitation : std::uint8_t {
    clear,
    rain,
    acid_rain,
    snow,
};

/** Inputs that are stable for one weather simulation tick. */
struct WeatherContext {
    Environment environment{Environment::unknown};
    bool indoors{};
    bool ridingSparrow{};
    float deltaSeconds{};
};

/** Effects that callers apply after a weather strategy is evaluated. */
struct WeatherFrame {
    Precipitation precipitation{Precipitation::clear};
    float damagePerTick{};
    float sparrowTraction{1.0F};
    bool renderIndoors{};
};

} // namespace dawn::server::runtime::weather
