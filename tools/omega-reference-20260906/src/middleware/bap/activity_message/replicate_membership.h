#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_writer.h"
#include "../../gameplay/descriptor/join_descriptor.h"
#include "activity_client_identity_parser.h"
#include "client_authoritative_data.h"

namespace dawn::middleware::bap::activity_message::replicate_membership {

/** Membership snapshots use activity message type 12. */
inline constexpr std::uint32_t kMessageType = 12;
/** Native membership has one region record per scenario bubble, not per state. */
inline constexpr std::size_t kRegionCount = 64;
inline constexpr std::int32_t kStatesPerBubble = 8;
inline constexpr std::int32_t kMaximumRegionIndex =
    static_cast<std::int32_t>(kRegionCount) * kStatesPerBubble - 1;

/** Packed active region for one bubble. -1 retains its legacy state-zero record. */
struct ActiveRegion final {
    std::int32_t index{-1};
};
/** One local player and the host-state tail are 29,968 meaningful bits. */
inline constexpr std::size_t kMeaningfulBitCount = 29'968;
/** The local-player snapshot is byte-aligned at 3,746 bytes. */
inline constexpr std::size_t kEncodedSize = 3'746;
/** Replacing one absent member with the activity host adds exactly 1,081 meaningful bits. */
inline constexpr std::size_t kRemoteMemberBitCount = 1'081;
/** One filled descriptor makes its record 1,024 bits longer and shifts every later field. */
inline constexpr std::size_t kDescriptorBitCount = gameplay::descriptor::kDescriptorSize * 8U;
/** Byte size once the activity host member and its region descriptor are both present. */
inline constexpr std::size_t kCitizenEncodedSize =
    kEncodedSize + (kRemoteMemberBitCount + kDescriptorBitCount + 7U) / 8U;
/** D4 has five presence bits and one selected u8, beyond its existing root bit. */
inline constexpr std::size_t kSynchronizationBitCount = 13;
inline constexpr std::size_t kMaximumEncodedSize =
    (kMeaningfulBitCount + kRemoteMemberBitCount + kDescriptorBitCount
     + 2U * kSynchronizationBitCount + 7U) / 8U;

/**
 * One remote-citizen advertisement placed in a single region record.
 * The record is picked by region index. The client adopts only the record whose index matches
 * its pending region.
 */
struct CitizenAdvertisement final {
    std::array<std::byte, gameplay::descriptor::kDescriptorSize> descriptor{};
    /** Host member key; identical to the descriptor's machine identity. */
    std::uint64_t memberKey{};
    /** Host NetAddr copied into member detail field 11. */
    std::array<std::byte, gameplay::descriptor::kNetAddrSize> address{};
    /** The ambassador's activity-host id. It is not the descriptor's own session id. */
    std::uint64_t onlineSessionId{};
    /** Region index of the record that carries it. */
    std::int32_t regionIndex{};
    /**
     * Ambassador member slot. It must differ from the joining client's own slot. An equal slot
     * picks the local-ambassador stage instead of the citizen stage.
     */
    std::uint8_t ambassadorSlot{};
    bool present{};
};

/** Inputs for one local-player membership snapshot. */
struct MembershipSnapshot final {
    client_identity::ClientIdentity identity{};
    client_authoritative_data::SpawnState spawn{};
    client_authoritative_data::TeleportState teleport{};
    /** Empty unless the gameplay channel is advertising an endpoint this run. */
    CitizenAdvertisement citizen{};
    std::uint32_t revision{};
    /** Stable session epoch; changing it clears the client's peer table. */
    std::uint32_t epoch{};
    /** Transition token copied into every member lane of every region. */
    std::uint8_t transitionToken{};
    /** Each explicit index must belong to its array bubble (index / 8). */
    std::array<ActiveRegion, kRegionCount> activeRegions{};
    /** Actual local D4 synchronization receipt, independent of the region token. */
    std::uint8_t synchronizationToken{};
    bool hasSynchronizationToken{};
    /** Host acknowledgement, supplied only after the caller's native readiness checks. */
    std::uint8_t hostSynchronizationToken{};
    bool hasHostSynchronizationToken{};
};

/** Selects a packed active state without changing any echoed transition token. */
[[nodiscard]] constexpr bool select_active_region(MembershipSnapshot& snapshot,
                                                  std::int32_t index) noexcept {
    if (index < 0 || index > kMaximumRegionIndex) { return false; }
    snapshot.activeRegions[static_cast<std::size_t>(index / kStatesPerBubble)].index = index;
    return true;
}

/** Default construction preserves every prior state-zero region record. */
[[nodiscard]] constexpr std::int32_t active_region(const MembershipSnapshot& snapshot,
                                                  std::size_t bubble) noexcept {
    if (bubble >= kRegionCount) { return -1; }
    const auto selected = snapshot.activeRegions[bubble].index;
    return selected == -1 ? static_cast<std::int32_t>(bubble) * kStatesPerBubble : selected;
}

[[nodiscard]] constexpr std::size_t synchronization_bits(const MembershipSnapshot& snapshot) noexcept {
    return (static_cast<std::size_t>(snapshot.hasSynchronizationToken)
            + static_cast<std::size_t>(snapshot.citizen.present && snapshot.hasHostSynchronizationToken))
           * kSynchronizationBitCount;
}

[[nodiscard]] constexpr std::size_t meaningful_bits(const MembershipSnapshot& snapshot) noexcept {
    return kMeaningfulBitCount
           + (snapshot.citizen.present ? kRemoteMemberBitCount + kDescriptorBitCount : 0U)
           + synchronization_bits(snapshot);
}

/** @return Exact byte size including optional native synchronization fields. */
[[nodiscard]] constexpr std::size_t encoded_size(const MembershipSnapshot& snapshot) noexcept {
    return (meaningful_bits(snapshot) + 7U) / 8U;
}
/**
 * Encodes one full-player membership snapshot. No allocation.
 * @param snapshot Checked identity, revision, transition, and host-echo values.
 * @param output Caller storage, left unchanged when validation fails or it is too small.
 * @param written Receives encoded_size(snapshot) on success or zero on failure.
 * @return True when the host-present body was encoded.
 */
[[nodiscard]] bool encode_replicate_membership(const MembershipSnapshot& snapshot,
                                               std::span<std::byte> output,
                                               std::size_t& written) noexcept;

/** The local member begins after root, revision, and epoch fields. */
inline constexpr std::size_t kMemberStartBit = 65;
/** The full local identity shifts the region block to bit 835. */
inline constexpr std::size_t kRegionBlockStartBit = 835;
/** The host-present region block ends before top-level field four. */
inline constexpr std::size_t kRegionBlockEndBit = 29'899;

/** @return Bit at which the region block ends for one snapshot. */
[[nodiscard]] constexpr std::size_t
region_block_end_bit(const MembershipSnapshot& snapshot) noexcept {
    return synchronization_bits(snapshot) + (snapshot.citizen.present
               ? kRegionBlockEndBit + kRemoteMemberBitCount + kDescriptorBitCount
               : kRegionBlockEndBit);
}

/** @return Bit at which the region block begins for one snapshot. */
[[nodiscard]] constexpr std::size_t
region_block_start_bit(const MembershipSnapshot& snapshot) noexcept {
    return synchronization_bits(snapshot) + (snapshot.citizen.present
               ? kRegionBlockStartBit + kRemoteMemberBitCount : kRegionBlockStartBit);
}

/** Validates wire widths, bubble ownership, and the exact advertised region. */
[[nodiscard]] bool valid(const MembershipSnapshot& snapshot) noexcept;

/**
 * Writes the local member and, when advertised, the non-local activity host.
 * @param writer Fixed-buffer writer positioned at bit 65.
 * @param snapshot Exact local identity and optional host member.
 * @return True when the writer reaches the region-block presence bit.
 */
[[nodiscard]] bool write_member_table(encoding::bits::Writer& writer,
                                      const MembershipSnapshot& snapshot) noexcept;

/**
 * Writes all 64 active region records and the host-present tail.
 * @param writer Fixed-buffer writer positioned at bit 835.
 * @param snapshot Transition token and reflected host state.
 * @return True when the writer reaches top-level field four.
 */
[[nodiscard]] bool write_region_block(encoding::bits::Writer& writer,
                                      const MembershipSnapshot& snapshot) noexcept;

} // namespace dawn::middleware::bap::activity_message::replicate_membership
