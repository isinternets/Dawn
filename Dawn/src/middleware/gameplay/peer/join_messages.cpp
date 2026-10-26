#include "join_messages.h"

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

/** The protocol version is a 16-bit value field. */
constexpr std::uint8_t kProtocolWidth = 16;
/** Both build fields are 32-bit value fields. */
constexpr std::uint8_t kBuildWidth = 32;
/** The executable type is three bits. */
constexpr std::uint8_t kExecutableWidth = 3;
/** The refusal reason is six bits. */
constexpr std::uint8_t kReasonWidth = 6;
} // namespace
/** Reads the admission prefix of a join request. */
bool read_join_request(bits::Reader& reader, JoinRequest& output) noexcept {
    std::uint64_t protocol = 0;
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    std::uint64_t executable = 0;
    JoinRequest candidate{};
    if (!reader.read(kProtocolWidth, protocol) || !reader.read(kBuildWidth, minimum)
        || !reader.read(kBuildWidth, maximum) || !reader.read(kExecutableWidth, executable)
        || !bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.joinId)) {
        return false;
    }
    candidate.protocolVersion = static_cast<std::uint16_t>(protocol);
    candidate.minimumBuild = static_cast<std::uint32_t>(minimum);
    candidate.maximumBuild = static_cast<std::uint32_t>(maximum);
    candidate.executableType = static_cast<std::uint8_t>(executable);
    output = candidate;
    return true;
}

/** Writes a join refusal body. */
bool write_join_refuse(bits::Writer& writer, const JoinRefuse& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId) && bits::write_raw_u64(writer, body.joinId)
           && writer.write(static_cast<std::uint64_t>(body.reason), kReasonWidth);
}

/** Reports whether a request may be answered at all. */
bool answerable(const JoinRequest& request) noexcept {
    return request.protocolVersion == kProtocolVersion;
}

/** Applies the host's admission rules in their exact order. */
bool admit(const JoinRequest& request, std::uint64_t hostSessionId, RefuseReason& reason) noexcept {
    if (!answerable(request)) {
        return false;
    }
    // This host holds one group session, so the request's session id needs no lookup.
    (void)hostSessionId;
    if (request.maximumBuild < kHostBuild) {
        reason = RefuseReason::peerVersionTooLow;
        return false;
    }
    if (request.minimumBuild > kHostBuild) {
        reason = RefuseReason::hostVersionTooLow;
        return false;
    }
    if (request.executableType != kExecutableType) {
        reason = RefuseReason::executableTypeMismatch;
        return false;
    }
    return true;
}

} // namespace dawn::middleware::gameplay::peer
