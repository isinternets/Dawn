#include "opcode501_codec.h"

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../../encoding/byte_order.h"
#include "../status_fields.h"

namespace dawn::middleware::web_service::messages::opcode501 {
namespace {

/** The create-character response carries the new character's 64-bit object id. */
constexpr std::uint8_t kCharacterSoidWidth = 64;

/**
 * Bit layout of the request, counted from the first payload bit. Only these three choices are
 * read. Each takes the wire value of its authored enum, and the bits between them are
 * per-request Client values.
 */
constexpr std::size_t kBitsBeforeRace = 7;
constexpr std::uint8_t kRaceWidth = 2;
constexpr std::size_t kBitsRaceToGender = 8;
constexpr std::uint8_t kGenderWidth = 1;
constexpr std::size_t kBitsGenderToClass = 7;
constexpr std::uint8_t kClassWidth = 2;
/** The last choice ends at bit 27, so four bytes hold every field this reads. */
constexpr std::size_t kMinimumPayloadSize = 4;
/** Largest wire value of each choice. */
constexpr std::uint64_t kMaximumRace = 2;
constexpr std::uint64_t kMaximumGender = 1;
constexpr std::uint64_t kMaximumClass = 2;

} // namespace

/** Reads race, gender and class from their fixed bit positions. */
bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() < kMinimumPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t race = 0;
    std::uint64_t gender = 0;
    std::uint64_t characterClass = 0;
    if (!reader.skip(kBitsBeforeRace) || !reader.read(kRaceWidth, race)
        || !reader.skip(kBitsRaceToGender) || !reader.read(kGenderWidth, gender)
        || !reader.skip(kBitsGenderToClass) || !reader.read(kClassWidth, characterClass)
        || race > kMaximumRace || gender > kMaximumGender || characterClass > kMaximumClass) {
        return false;
    }
    request.race = static_cast<std::uint8_t>(race);
    request.gender = static_cast<std::uint8_t>(gender);
    request.characterClass = static_cast<std::uint8_t>(characterClass);
    return true;
}

/** Writes the echoed header, the shared status pair, the character id, and the trailer. */
bool encode_response(const Message& message,
                     std::uint64_t characterSoid,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kEnvelopeHeaderSize) {
        return false;
    }
    encoding::write_u16_be(output.first<encoding::kU16Size>(), message.opcode);
    encoding::write_u32_be(output.subspan<encoding::kU16Size, encoding::kU32Size>(),
                           message.transactionId);

    encoding::bits::Writer writer(output.subspan(kEnvelopeHeaderSize));
    // The id carries no presence bit or bias.
    bool encoded = status::write_fields(writer, ResponseShape::statusPair, StatusResponse{})
                   && writer.write(characterSoid, kCharacterSoidWidth)
                   && writer.write(0, kAbsentTrailerWidth);
    std::size_t payloadSize = 0;
    if (!encoded || !writer.finish(payloadSize)) {
        return false;
    }
    written = kEnvelopeHeaderSize + payloadSize;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode501
