#include "activity_high_water_validator.h"
namespace dawn::middleware::bap::activity_message::high_water {

/** Checks one opaque high-water body without reading any payload byte. */
bool validate_high_water(std::span<const std::byte> input) noexcept {
    return input.size() == kEncodedSize;
}

} // namespace dawn::middleware::bap::activity_message::high_water
