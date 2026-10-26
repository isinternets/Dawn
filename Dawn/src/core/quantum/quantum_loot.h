#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace dawn::core::quantum {

using Qubit = std::optional<bool>;

struct Engram {
    Qubit exotic;
    bool observed{};
};

[[nodiscard]] inline bool collapse_inventory(Engram& engram, bool clientObservation) noexcept {
    if (!engram.exotic) {
        engram.exotic = clientObservation;
    }
    engram.observed = true;
    return *engram.exotic;
}

template <class Vendor, class Predicate>
[[nodiscard]] const Vendor* grover_vendor_search(std::span<const Vendor> vendors, Predicate matches) {
    const auto probes = static_cast<std::size_t>(std::ceil(std::sqrt(vendors.size())));
    for (std::size_t offset = 0; offset < probes; ++offset) {
        for (std::size_t index = offset; index < vendors.size(); index += probes) {
            if (matches(vendors[index])) {
                return &vendors[index];
            }
        }
    }
    return nullptr;
}

} // namespace dawn::core::quantum
