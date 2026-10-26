#include "introduction.h"

namespace dawn::middleware::gameplay::nat {

namespace {

/** The message type opens the introduction. */
constexpr std::size_t kTypeOffset = 0;
/** Type 13 is a direct introduction request, sent to every address a peer advertises. */
constexpr std::byte kRequestType{13};
/** Type 12 is the reply the requester waits for. It resolves the address it arrived from. */
constexpr std::byte kReplyType{12};

} // namespace
/** Turns one direct introduction request into its reply, in place. */
bool make_introduction_reply(std::span<std::byte> datagram) noexcept {
    if (datagram.size() != kIntroductionSize || datagram[kTypeOffset] != kRequestType) {
        return false;
    }
    datagram[kTypeOffset] = kReplyType;
    return true;
}

} // namespace dawn::middleware::gameplay::nat
