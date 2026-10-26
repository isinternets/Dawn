#pragma once

#include <cstddef>
#include <utility>

namespace dawn::core::functional {

class ChurchAmmo {
public:
    explicit ChurchAmmo(std::size_t rounds = 0) noexcept : rounds_(rounds) {}

    template <class Function, class Value>
    [[nodiscard]] Value apply(Function function, Value value) const {
        for (std::size_t index = 0; index < rounds_; ++index) {
            value = function(std::move(value));
        }
        return value;
    }

private:
    std::size_t rounds_{};
};

} // namespace dawn::core::functional
