#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../../definition.h"

namespace dawn::state::activity::entity_slots::transactions {

/** @return True when no lease bit is set. */
inline bool empty(const LeaseMask& mask) noexcept {
    for (const std::byte value : mask) {
        if (value != std::byte{}) {
            return false;
        }
    }
    return true;
}

/**
 * Picks free logical slots in ascending order.
 * @param held Slots the new grant cannot use.
 * @param requestedCount Most slots to pick.
 * @return Mask with at most requestedCount free bits set.
 */
inline LeaseMask select_free(const LeaseMask& held, std::size_t requestedCount) noexcept {
    LeaseMask selected{};
    const std::size_t wanted = (std::min)(requestedCount, kSlotCount);
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < kSlotCount && count < wanted; ++slot) {
        const std::size_t byteIndex = slot / kSlotsPerByte;
        const std::size_t bitIndex = slot % kSlotsPerByte;
        const std::byte bit{static_cast<unsigned char>(1U << bitIndex)};
        if ((held[byteIndex] & bit) != std::byte{}) {
            continue;
        }
        selected[byteIndex] |= bit;
        ++count;
    }
    return selected;
}

/** @return Bits set in either lease mask. */
inline LeaseMask unite(const LeaseMask& first, const LeaseMask& second) noexcept {
    LeaseMask result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = first[index] | second[index];
    }
    return result;
}

/**
 * Reserves the highest logical slots.
 * The client is granted low indices, so taking the high end keeps one contiguous split and
 * leaves the client's ascending selection unchanged.
 * @param count Slots to reserve, capped at the slot count.
 * @return Mask with the highest count bits set.
 */
inline LeaseMask reserve_high(std::size_t count) noexcept {
    LeaseMask reserved{};
    const std::size_t wanted = (std::min)(count, kSlotCount);
    for (std::size_t index = 0; index < wanted; ++index) {
        const std::size_t slot = kSlotCount - 1 - index;
        reserved[slot / kSlotsPerByte] |=
            std::byte{static_cast<unsigned char>(1U << (slot % kSlotsPerByte))};
    }
    return reserved;
}

/** @return Bits set in both lease masks. */
inline LeaseMask intersect(const LeaseMask& first, const LeaseMask& second) noexcept {
    LeaseMask result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = first[index] & second[index];
    }
    return result;
}

/** @return True when both masks hold the same bits. */
inline bool equal(const LeaseMask& first, const LeaseMask& second) noexcept {
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

/** @return True when selected contains a bit absent from available. */
inline bool exceeds(const LeaseMask& selected, const LeaseMask& available) noexcept {
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if ((selected[index] & ~available[index]) != std::byte{}) {
            return true;
        }
    }
    return false;
}
} // namespace dawn::state::activity::entity_slots::transactions
