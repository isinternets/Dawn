#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode502 {

/** Web Service opcode for the delete-character request. */
inline constexpr std::uint16_t kOpcode = 502;

/** The character the player confirmed deleting. */
struct Request {
    std::uint64_t characterSoid{};
};

/**
 * Parses the bare 64-bit character id.
 * The id is taken as sent. Only its width is required.
 * @param message Parsed Web Service envelope.
 * @param request Receives the character id.
 * @return True when the whole id is present.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode502
