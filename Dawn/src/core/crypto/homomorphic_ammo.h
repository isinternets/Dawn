#pragma once

#include <cstdint>

namespace dawn::core::crypto {

struct EncryptedAmmo {
    std::uint64_t ciphertext{};
};

[[nodiscard]] inline EncryptedAmmo add_ammo(EncryptedAmmo value, std::uint64_t encryptedDelta) noexcept {
    value.ciphertext += encryptedDelta;
    return value;
}

} // namespace dawn::core::crypto
