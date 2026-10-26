#pragma once

#include "weather_strategy_provider_factory.h"
#include <memory>

namespace dawn::server::runtime::weather {

/** Coordinates the single active precipitation strategy for a hosted patrol space. */
class rain_singleton_manager_impl final {
public:
    /** Replaces the active strategy whenever the hosted environment changes. */
    void enter(Environment environment) {
        environment_ = environment;
        strategy_ = factory_.create(environment);
    }

    /** Evaluates the active strategy, or clear weather before an environment is entered. */
    [[nodiscard]] WeatherFrame tick(const WeatherContext& context) const noexcept {
        return strategy_ ? strategy_->evaluate(context) : WeatherFrame{};
    }

    /** Returns the environment whose strategy is currently active. */
    [[nodiscard]] Environment environment() const noexcept { return environment_; }

private:
    WeatherStrategyProviderFactory factory_;
    std::unique_ptr<IWeatherStrategy> strategy_;
    Environment environment_{Environment::unknown};
};

} // namespace dawn::server::runtime::weather
