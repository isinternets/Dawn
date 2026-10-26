#pragma once

#include "weather_types.h"

namespace dawn::server::runtime::weather {

/** Computes the immutable weather result for a single simulation tick. */
class IWeatherStrategy {
public:
    virtual ~IWeatherStrategy() = default;

    /** Evaluates weather without retaining references to the caller's context. */
    [[nodiscard]] virtual WeatherFrame evaluate(const WeatherContext& context) const noexcept = 0;
};

} // namespace dawn::server::runtime::weather
