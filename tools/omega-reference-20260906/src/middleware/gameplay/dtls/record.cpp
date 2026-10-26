#include "record.h"

#include <algorithm>

#include "../../crypto/aes_cbc.h"
#include "../../crypto/tiger192.h"
#include "dtls_messages.h"

namespace dawn::middleware::gameplay::dtls {

namespace {

/** Bits in one byte. */
constexpr unsigned kByteBits = 8;
/** Mask of one byte. */
constexpr std::uint32_t kByteMask = 0xFF;
/** The addressed tag sits inside the shared header. */
constexpr std::size_t kTagOffset = 2;
/** The sequence number closes the shared header. */
constexpr std::size_t kSequenceOffset = 4;
/** The authentication tag follows the header. */
constexpr std::size_t kAuthOffset = kHeaderSize;
/** The plaintext length follows the authentication tag. */
constexpr std::size_t kLengthOffset = kAuthOffset + kRecordTagSize;
/** The ciphertext follows the length. */
constexpr std::size_t kCipherOffset = kRecordPrefixSize;
/** The filler the peer writes between the plaintext end and the block boundary. */
constexpr std::byte kFiller{0x01};
/** The sequence and the tag are hashed together to make the vector. */
constexpr std::size_t kVectorSeedSize = 6;
/** Type value that marks a record. It shares the type field with the handshake messages. */
constexpr std::byte kRecordType{6};
/** A constant the sender writes into the header and never reads back. */
constexpr std::byte kVersion{2};

/**
 * Writes one unsigned value low byte first.
 * @param output Field storage.
 * @param offset First byte of the field.
 * @param value Host-order value.
 * @param width Field width in bytes.
 */
void write_memory_order(std::span<std::byte> output,
                        std::size_t offset,
                        std::uint32_t value,
                        std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        output[offset + index] = static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
    }
}

/**
 * Reads one unsigned value, low byte first.
 * @param datagram Record bytes.
 * @param offset First byte of the field.
 * @param width Field width in bytes.
 * @return Host-order value.
 */
[[nodiscard]] std::uint32_t read_memory_order(std::span<const std::byte> datagram,
                                              std::size_t offset,
                                              std::size_t width) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= std::to_integer<std::uint32_t>(datagram[offset + index]) << (index * kByteBits);
    }
    return value;
}
/**
 * Builds the record's initialization vector.
 * @param sequence Record sequence number.
 * @param tag Record addressed tag.
 * @param output Receives one block.
 */
void build_vector(std::uint32_t sequence,
                  std::uint16_t tag,
                  std::array<std::byte, crypto::aes::kBlockSize>& output) noexcept {
    std::array<std::byte, kVectorSeedSize> seed{};
    write_memory_order(seed, 0, sequence, sizeof(std::uint32_t));
    write_memory_order(seed, sizeof(std::uint32_t), tag, sizeof(std::uint16_t));
    crypto::tiger::Digest digest{};
    crypto::tiger::hash(seed, digest);
    std::copy_n(digest.begin(), output.size(), output.begin());
}

/**
 * Authenticates one record.
 * The tag covers the header and everything from the length field on, but not the tag bytes.
 * @param algorithm Digest to key.
 * @param key Authentication key.
 * @param datagram Whole record.
 * @param output Receives the leading tag bytes.
 * @return True when the digest completed.
 */
[[nodiscard]] bool authenticate(crypto::hmac::Algorithm algorithm,
                                std::span<const std::byte> key,
                                std::span<const std::byte> datagram,
                                std::array<std::byte, kRecordTagSize>& output) noexcept {
    crypto::hmac::Digest digest{};
    if (!crypto::hmac::authenticate(
            algorithm, key, datagram.first(kHeaderSize), datagram.subspan(kLengthOffset), digest)) {
        return false;
    }
    std::copy_n(digest.bytes.begin(), output.size(), output.begin());
    return true;
}

/**
 * @param plaintext Plaintext length.
 * @return The ciphertext length that plaintext length produces.
 */
[[nodiscard]] std::size_t padded_length(std::size_t plaintext) noexcept {
    const std::size_t remainder = plaintext % crypto::aes::kBlockSize;
    return remainder == 0 ? plaintext : plaintext + (crypto::aes::kBlockSize - remainder);
}

/**
 * Checks the declared length against the record it arrived in.
 * @param datagram Whole record.
 * @param plaintext Receives the declared plaintext length.
 * @return True when the record is long enough and its ciphertext is whole blocks.
 */
[[nodiscard]] bool shaped(std::span<const std::byte> datagram, std::size_t& plaintext) noexcept {
    if (datagram.size() <= kRecordPrefixSize) {
        return false;
    }
    plaintext = read_memory_order(datagram, kLengthOffset, sizeof(std::uint16_t));
    const std::size_t cipher = datagram.size() - kCipherOffset;
    return plaintext != 0 && padded_length(plaintext) == cipher
           && cipher % crypto::aes::kBlockSize == 0 && plaintext <= kRecordPayloadCapacity;
}

/**
 * Compares two tags without an early exit.
 * @param left First tag.
 * @param right Second tag.
 * @return True when every byte matches.
 */
[[nodiscard]] bool same_tag(std::span<const std::byte> left,
                            const std::array<std::byte, kRecordTagSize>& right) noexcept {
    std::byte difference{0};
    for (std::size_t index = 0; index < right.size(); ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == std::byte{0};
}

} // namespace

/** Reads the tag one received record is addressed to, without opening it. */
bool read_record_tag(std::span<const std::byte> datagram, std::uint16_t& tag) noexcept {
    tag = 0;
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    tag =
        static_cast<std::uint16_t>(read_memory_order(datagram, kTagOffset, sizeof(std::uint16_t)));
    return true;
}

/** Opens one received record. */
bool open(const RecordContext& context,
          std::span<const std::byte> datagram,
          std::span<std::byte> plaintext,
          std::size_t& size,
          std::uint32_t& sequence) noexcept {
    std::size_t declared = 0;
    if (!shaped(datagram, declared) || plaintext.size() < padded_length(declared)) {
        return false;
    }
    std::array<std::byte, kRecordTagSize> expected{};
    if (!authenticate(context.authAlgorithm, context.keys.auth, datagram, expected)
        || !same_tag(datagram.subspan(kAuthOffset, kRecordTagSize), expected)) {
        return false;
    }

    sequence = read_memory_order(datagram, kSequenceOffset, sizeof(std::uint32_t));
    const auto tag =
        static_cast<std::uint16_t>(read_memory_order(datagram, kTagOffset, sizeof(std::uint16_t)));
    std::array<std::byte, crypto::aes::kBlockSize> vector{};
    build_vector(sequence, tag, vector);
    if (!crypto::aes::decrypt(
            context.keys.cypher, vector, datagram.subspan(kCipherOffset), plaintext)) {
        return false;
    }
    size = declared;
    return true;
}

/** Builds one record for sending. */
bool seal(const RecordContext& context,
          std::uint32_t sequence,
          std::span<const std::byte> payload,
          std::span<std::byte> datagram,
          std::size_t& size) noexcept {
    const std::size_t cipher = padded_length(payload.size());
    const std::size_t total = kCipherOffset + cipher;
    if (payload.empty() || payload.size() > kRecordPayloadCapacity || datagram.size() < total) {
        return false;
    }
    std::fill_n(datagram.begin(), total, std::byte{0});
    datagram[0] = kRecordType;
    datagram[1] = kVersion;
    write_memory_order(datagram, kTagOffset, context.sendTag, sizeof(std::uint16_t));
    write_memory_order(datagram, kSequenceOffset, sequence, sizeof(std::uint32_t));
    write_memory_order(
        datagram, kLengthOffset, static_cast<std::uint32_t>(payload.size()), sizeof(std::uint16_t));
    std::copy(payload.begin(), payload.end(), datagram.begin() + kCipherOffset);
    // The peer fills the tail of the last block with this byte and never reads it back.
    const std::span<std::byte> filler =
        datagram.subspan(kCipherOffset + payload.size(), cipher - payload.size());
    std::fill(filler.begin(), filler.end(), kFiller);

    std::array<std::byte, crypto::aes::kBlockSize> vector{};
    build_vector(sequence, context.sendTag, vector);
    const std::span<std::byte> region = datagram.subspan(kCipherOffset, cipher);
    if (!crypto::aes::encrypt(context.keys.cypher, vector, region, region)) {
        return false;
    }
    std::array<std::byte, kRecordTagSize> tag{};
    if (!authenticate(context.authAlgorithm, context.keys.auth, datagram.first(total), tag)) {
        return false;
    }
    std::copy(tag.begin(), tag.end(), datagram.begin() + kAuthOffset);
    size = total;
    return true;
}

/** Reports which digest authenticates a received record. */
bool identify_auth(const Keys& keys,
                   std::span<const std::byte> datagram,
                   crypto::hmac::Algorithm& output) noexcept {
    std::size_t declared = 0;
    if (!shaped(datagram, declared)) {
        return false;
    }
    for (const crypto::hmac::Algorithm candidate : {crypto::hmac::Algorithm::murmur3,
                                                    crypto::hmac::Algorithm::sha256,
                                                    crypto::hmac::Algorithm::sha1}) {
        std::array<std::byte, kRecordTagSize> expected{};
        if (authenticate(candidate, keys.auth, datagram, expected)
            && same_tag(datagram.subspan(kAuthOffset, kRecordTagSize), expected)) {
            output = candidate;
            return true;
        }
    }
    return false;
}

} // namespace dawn::middleware::gameplay::dtls
