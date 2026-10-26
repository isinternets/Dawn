#pragma once

#include <optional>
#include <utility>

namespace dawn::core::functional {

template <class Character, class Jump>
[[nodiscard]] std::optional<Character> monad_jump_button(std::optional<Character> character, Jump jump) {
    if (!character) {
        return std::nullopt;
    }
    return std::optional<Character>{jump(std::move(*character))};
}

} // namespace dawn::core::functional
