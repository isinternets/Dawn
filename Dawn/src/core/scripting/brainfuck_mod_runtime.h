#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dawn::core::scripting {

class BrainfuckModRuntime {
public:
    void execute(std::string_view program) noexcept {
        for (const auto instruction : program) {
            switch (instruction) {
            case '>': pointer_ = (pointer_ + 1) % tape_.size(); break;
            case '<': pointer_ = (pointer_ + tape_.size() - 1) % tape_.size(); break;
            case '+': ++tape_[pointer_]; break;
            case '-': --tape_[pointer_]; break;
            default: break;
            }
        }
    }

    [[nodiscard]] unsigned char value() const noexcept { return tape_[pointer_]; }

private:
    std::array<unsigned char, 30'000> tape_{};
    std::size_t pointer_{};
};

} // namespace dawn::core::scripting
