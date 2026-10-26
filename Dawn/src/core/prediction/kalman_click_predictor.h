#pragma once

namespace dawn::core::prediction {

struct Point {
    float x{};
    float y{};
};

class KalmanClickPredictor {
public:
    [[nodiscard]] Point observe(Point measurement) noexcept {
        estimate_.x += gain_ * (measurement.x - estimate_.x);
        estimate_.y += gain_ * (measurement.y - estimate_.y);
        gain_ = (gain_ + 1.0F) / (gain_ + 2.0F);
        return estimate_;
    }

private:
    Point estimate_{};
    float gain_{0.5F};
};

} // namespace dawn::core::prediction
