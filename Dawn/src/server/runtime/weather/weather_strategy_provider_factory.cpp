#include "weather_strategy_provider_factory.h"

namespace dawn::server::runtime::weather {
namespace {

class PatrolWeatherStrategy final : public IWeatherStrategy {
public:
    explicit PatrolWeatherStrategy(Environment environment) noexcept : environment_(environment) {}

    [[nodiscard]] WeatherFrame evaluate(const WeatherContext& context) const noexcept override {
        switch (environment_) {
        case Environment::cosmodrome:
            return {Precipitation::acid_rain, 1.0F, 1.0F, context.indoors};
        case Environment::mars:
            return {Precipitation::rain, 0.0F, context.ridingSparrow ? 0.35F : 1.0F, false};
        case Environment::tower:
            return {Precipitation::snow, 0.0F, 1.0F, true};
        case Environment::unknown:
        default:
            return {};
        }
    }

private:
    Environment environment_;
};

} // namespace

std::unique_ptr<IWeatherStrategy>
WeatherStrategyProviderFactory::create(Environment environment) const {
    return std::make_unique<PatrolWeatherStrategy>(environment);
}

} // namespace dawn::server::runtime::weather
