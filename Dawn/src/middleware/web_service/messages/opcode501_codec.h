#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode501 {

/** Web Service opcode for the create-character request. */
inline constexpr std::uint16_t kOpcode = 501;

/**
 * The choices the create-character request carries that the server acts on. Values are the
 * Client's own, which are the wire values of the authored race, gender and class enums.
 */
struct Request {
    /** 0 Human, 1 Awoken, 2 Exo. */
    std::uint8_t race{};
    /** 0 male, 1 female. */
    std::uint8_t gender{};
    /** 0 Titan, 1 Hunter, 2 Warlock. */
    std::uint8_t characterClass{};
};

/**
 * Parses the identity choices out of the bit-packed create-character request.
 * Everything else in the body is left unread: the Client fills it with per-request values that
 * change on every send.
 * @param message Parsed Web Service envelope.
 * @param request Receives the choices; left empty on failure.
 * @return True when the body is long enough and every choice is in range.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

/**
 * Encodes the create-character response: the status pair then the character object id.
 * @param message Parsed request whose envelope fields are echoed.
 * @param characterSoid Character object id; must be published in family 3.
 * @param output Caller-owned svc-11 response-body storage.
 * @param written Receives encoded response-body bytes.
 * @return True when the fixed response fits the output buffer.
 */
[[nodiscard]] bool encode_response(const Message& message,
                                   std::uint64_t characterSoid,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode501
