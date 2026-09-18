#include <array>
#include <atomic>
#include <cstdio>
#include <limits>

#include "../../../../../core/logging/log.h"
#include "../../internal.h"
#include "../../policy/policy.h"
#include "../discovery/egress_discovery_responder.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::winsock::transmission {
namespace {

/** A null destination picks the connected peer only when the address length is 0. */
constexpr int kConnectedDestinationLength = 0;

/** Retail's local gameplay port and Dawn's collision-free embedded-host port. */
constexpr std::uint16_t kRetailGameplayPort = 30976;
constexpr std::uint16_t kEmbeddedGameplayPort = 30978;
/** Enough reports to cover traversal, DTLS and association without logging normal gameplay. */
constexpr unsigned kMaximumGameplaySendReports = 96;

std::atomic<unsigned> g_gameplaySendReports{0};

[[nodiscard]] bool gameplay_destination(const sockaddr* destination,
                                        int destinationLength) noexcept {
    if (destination == nullptr || destinationLength < static_cast<int>(sizeof(sockaddr_in))
        || destination->sa_family != AF_INET) {
        return false;
    }
    const auto* const ipv4 = reinterpret_cast<const sockaddr_in*>(destination);
    const std::uint16_t port = ntohs(ipv4->sin_port);
    return port == kRetailGameplayPort || port == kEmbeddedGameplayPort;
}

/**
 * Reports the bounded client side of the gameplay UDP path. This is deliberately after the real
 * send so a successful rewrite, a Winsock refusal and an asynchronous pending send are distinct.
 */
void log_gameplay_send(const char* api,
                       SOCKET socket,
                       const sockaddr* destination,
                       int destinationLength,
                       std::size_t bytes,
                       unsigned firstByte,
                       int result,
                       int error) noexcept {
    if (!gameplay_destination(destination, destinationLength)
        || g_gameplaySendReports.fetch_add(1, std::memory_order_relaxed)
               >= kMaximumGameplaySendReports) {
        return;
    }
    const auto* const remote = reinterpret_cast<const sockaddr_in*>(destination);
    sockaddr_in local{};
    int localLength = sizeof local;
    const bool hasLocal = ::getsockname(
                              socket, reinterpret_cast<sockaddr*>(&local), &localLength)
                          != SOCKET_ERROR;
    std::array<char, 384> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=egress stage=gameplay_send api=%s socket=%llu local=0x%08X:%u remote=0x%08X:%u bytes=%zu b0=0x%02X result=%d error=%d",
        api,
        static_cast<unsigned long long>(socket),
        hasLocal ? static_cast<unsigned>(ntohl(local.sin_addr.s_addr)) : 0U,
        hasLocal ? static_cast<unsigned>(ntohs(local.sin_port)) : 0U,
        static_cast<unsigned>(ntohl(remote->sin_addr.s_addr)),
        static_cast<unsigned>(ntohs(remote->sin_port)),
        bytes,
        firstByte,
        result,
        error);
    if (written > 0) {
        const std::size_t length = static_cast<std::size_t>(written) < line.size()
                                       ? static_cast<std::size_t>(written)
                                       : line.size() - 1;
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         std::string_view(line.data(), length));
    }
}

/** Clears an optional send count before a denial. */
void clear_bytes(LPDWORD bytes) noexcept {
    if (bytes != nullptr) {
        *bytes = 0;
    }
}

/**
 * Redirects an explicit IPv4 destination, or checks a connected redirect-target peer.
 * @param socket Socket used when the destination is omitted.
 * @param destination Optional destination address.
 * @param destinationLength Available destination bytes.
 * @param redirected Receives the rewritten explicit destination.
 * @param forwardedDestination Receives null or the rewritten destination.
 * @param forwardedLength Receives the destination byte count for the original.
 * @return True only when the original can target the exact IPv4 redirect target.
 */
[[nodiscard]] bool prepare_destination(SOCKET socket,
                                       const sockaddr* destination,
                                       int destinationLength,
                                       sockaddr_in& redirected,
                                       sockaddr*& forwardedDestination,
                                       int& forwardedLength) noexcept {
    if (destination != nullptr) {
        if (!policy::redirect_ipv4(destination, destinationLength, redirected)) {
            return false;
        }
        forwardedDestination = reinterpret_cast<sockaddr*>(&redirected);
        forwardedLength = static_cast<int>(sizeof(redirected));
        return true;
    }
    forwardedDestination = nullptr;
    forwardedLength = kConnectedDestinationLength;
    return destinationLength == kConnectedDestinationLength
           && policy::has_redirect_target_peer(socket);
}

/** Reports a discovery request the local responder could not answer. */
void log_discovery(bool succeeded) noexcept {
    if (succeeded) {
        return;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::warn,
                     "ev=egress stage=discovery target=redirect action=respond result=fail");
}

/**
 * Adapts one synchronous WSA buffer to the local discovery responder.
 * @param overlapped Optional asynchronous state.
 * @param completion Optional completion callback.
 * @param sendTo The original sendto entry.
 * @return Local discovery result, or an unhandled result.
 */
[[nodiscard]] discovery::Result
handle_buffer_discovery(SOCKET socket,
                        LPWSABUF buffers,
                        DWORD bufferCount,
                        DWORD flags,
                        const sockaddr* destination,
                        int destinationLength,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion,
                        discovery::SendTo sendTo) noexcept {
    if (buffers == nullptr || bufferCount != 1 || overlapped != nullptr || completion != nullptr
        || buffers[0].len > static_cast<ULONG>((std::numeric_limits<int>::max)())) {
        return {};
    }
    return discovery::handle(socket,
                             std::as_bytes(std::span(buffers[0].buf, buffers[0].len)),
                             static_cast<int>(flags),
                             destination,
                             destinationLength,
                             sendTo);
}

} // namespace

/** Handles local discovery or redirects one datagram to the redirect target. */
int WSAAPI send_bytes_to(SOCKET socket,
                         const char* buffer,
                         int length,
                         int flags,
                         const sockaddr* destination,
                         int destinationLength) noexcept {
    const auto call = original<decltype(&::sendto)>(HookSlot::sendTo);
    if (buffer != nullptr && length >= 0) {
        const discovery::Result discoveryResult =
            discovery::handle(socket,
                              std::as_bytes(std::span(buffer, static_cast<std::size_t>(length))),
                              flags,
                              destination,
                              destinationLength,
                              call);
        if (discoveryResult.handled) {
            log_discovery(discoveryResult.result != SOCKET_ERROR);
            return discoveryResult.result;
        }
    }

    sockaddr_in redirected{};
    sockaddr* forwardedDestination = nullptr;
    int forwardedLength = 0;
    const bool targetsRedirect = prepare_destination(
        socket, destination, destinationLength, redirected, forwardedDestination, forwardedLength);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    const int result =
        call(socket, buffer, length, flags, forwardedDestination, forwardedLength);
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    log_gameplay_send("sendto",
                      socket,
                      forwardedDestination,
                      forwardedLength,
                      length > 0 ? static_cast<std::size_t>(length) : 0U,
                      buffer != nullptr && length > 0
                          ? static_cast<unsigned>(static_cast<unsigned char>(buffer[0]))
                          : 0U,
                      result,
                      error);
    if (result == SOCKET_ERROR) {
        WSASetLastError(error);
    }
    return result;
}

/** Handles local discovery or redirects vectored datagrams to the redirect target. */
int WSAAPI send_buffers_to(SOCKET socket,
                           LPWSABUF buffers,
                           DWORD bufferCount,
                           LPDWORD bytesSent,
                           DWORD flags,
                           const sockaddr* destination,
                           int destinationLength,
                           LPWSAOVERLAPPED overlapped,
                           LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSASendTo)>(HookSlot::wsaSendTo);
    const auto sendTo = original<decltype(&::sendto)>(HookSlot::sendTo);
    const discovery::Result discoveryResult = handle_buffer_discovery(socket,
                                                                      buffers,
                                                                      bufferCount,
                                                                      flags,
                                                                      destination,
                                                                      destinationLength,
                                                                      overlapped,
                                                                      completion,
                                                                      sendTo);
    if (discoveryResult.handled) {
        const bool succeeded = discoveryResult.result != SOCKET_ERROR;
        if (bytesSent != nullptr) {
            *bytesSent = succeeded ? buffers[0].len : 0;
        }
        log_discovery(succeeded);
        return succeeded ? 0 : SOCKET_ERROR;
    }

    sockaddr_in redirected{};
    sockaddr* forwardedDestination = nullptr;
    int forwardedLength = 0;
    const bool targetsRedirect = prepare_destination(
        socket, destination, destinationLength, redirected, forwardedDestination, forwardedLength);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        clear_bytes(bytesSent);
        return policy::deny_socket_call();
    }
    const int result = call(socket,
                            buffers,
                            bufferCount,
                            bytesSent,
                            flags,
                            forwardedDestination,
                            forwardedLength,
                            overlapped,
                            completion);
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    std::size_t bytes = 0;
    unsigned firstByte = 0;
    for (DWORD index = 0; buffers != nullptr && index < bufferCount; ++index) {
        bytes += buffers[index].len;
        if (firstByte == 0 && buffers[index].buf != nullptr && buffers[index].len != 0) {
            firstByte = static_cast<unsigned>(
                static_cast<unsigned char>(buffers[index].buf[0]));
        }
    }
    log_gameplay_send("WSASendTo",
                      socket,
                      forwardedDestination,
                      forwardedLength,
                      bytes,
                      firstByte,
                      result,
                      error);
    if (result == SOCKET_ERROR) {
        WSASetLastError(error);
    }
    return result;
}

} // namespace dawn::client::hooks::egress::winsock::transmission
