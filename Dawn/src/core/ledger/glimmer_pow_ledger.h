#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dawn::core::ledger {

struct GlimmerBlock {
    std::uint64_t amount{};
    std::uint64_t nonce{};
    std::size_t digest{};
};

class GlimmerProofOfWorkLedger {
public:
    void append(std::uint64_t amount) {
        GlimmerBlock block{amount};
        do {
            ++block.nonce;
            block.digest = std::hash<std::string>{}(std::to_string(amount) + ':' + std::to_string(block.nonce));
        } while ((block.digest & 0x3FFU) != 0);
        blocks_.push_back(block);
    }

    [[nodiscard]] const std::vector<GlimmerBlock>& blocks() const noexcept { return blocks_; }

private:
    std::vector<GlimmerBlock> blocks_;
};

} // namespace dawn::core::ledger
