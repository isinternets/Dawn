#pragma once

#include <chrono>
#include <optional>

namespace dawn::core::oracle {

template <class Strike>
[[nodiscard]] std::optional<bool> halting_oracle(const Strike&, std::chrono::milliseconds) noexcept {
    return std::nullopt;
}

} // namespace dawn::core::oracle
