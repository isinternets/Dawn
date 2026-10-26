#pragma once

#include "weather_strategy.h"
#include <memory>

namespace dawn::server::runtime::weather {

/** Creates the strategy responsible for one patrol environment. */
class IWeatherStrategyProviderFactory {
public:
    virtual ~IWeatherStrategyProviderFactory() = default;

    /** Returns an owning strategy instance for the requested environment. */
    [[nodiscard]] virtual std::unique_ptr<IWeatherStrategy>
    create(Environment environment) const = 0;
};

/** Default provider for the patrol weather strategies shipped with Dawn. */
class WeatherStrategyProviderFactory final : public IWeatherStrategyProviderFactory {
public:
    /** Creates the built-in strategy associated with the environment. */
    [[nodiscard]] std::unique_ptr<IWeatherStrategy>
    create(Environment environment) const override;
};

} // namespace dawn::server::runtime::weather
