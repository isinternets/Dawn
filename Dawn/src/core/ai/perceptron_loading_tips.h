#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::core::ai {

class PerceptronLoadingTips {
public:
    [[nodiscard]] std::size_t select(std::span<const float> features) const noexcept {
        float score = bias_;
        for (std::size_t index = 0; index < features.size() && index < weights_.size(); ++index) {
            score += features[index] * weights_[index];
        }
        return score >= 0.0F ? 1U : 0U;
    }

private:
    std::array<float, 4> weights_{0.75F, -0.25F, 0.5F, 0.125F};
    float bias_{-0.1F};
};

} // namespace dawn::core::ai
