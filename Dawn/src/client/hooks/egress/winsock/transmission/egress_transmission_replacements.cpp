#include <array>
#include <atomic>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../core/settings/settings.h"
#include "../../internal.h"
#include "../../policy/policy.h"
#include "replacements.h"

namespace dawn::client::hooks::egress::winsock::transmission {
namespace {

/** Retail's historical port remains useful when diagnosing a stale descriptor. */
constexpr std::uint16_t kRetailGameplayPort = 30976;
/** Connected sends reported per run before normal gameplay traffic is suppressed. */
constexpr unsigned kMaximumGameplaySendReports = 96;

std::atomic<unsigned> g_gameplaySendReports{0};

/**
 * Sets a fixed byte count before a blocked overlapped send returns.
 * @param bytes Optional caller-owned byte count.
 */
void clear_bytes(LPDWORD bytes) noexcept {
    if (bytes != nullptr) {
        *bytes = 0;
    }
}

/** Reads a connected peer only when it belongs to the configured gameplay channel. */
[[nodiscard]] bool gameplay_peer(SOCKET socket, sockaddr_in& peer) noexcept {
    int peerLength = static_cast<int>(sizeof(peer));
    if (::getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peerLength) == SOCKET_ERROR
        || peer.sin_family != AF_INET) {
        return false;
    }
    const std::uint16_t port = ntohs(peer.sin_port);
    return port == kRetailGameplayPort
           || port == core::settings::get().server.gameplay.port;
}

/** Reports connected gameplay sends, including their actual post-connect local and remote tuple. */
void report_gameplay_send(const char* api,
                          SOCKET socket,
                          std::size_t bytes,
                          unsigned firstByte,
                          int result,
                          int error) noexcept {
    sockaddr_in peer{};
    if (!gameplay_peer(socket, peer)
        || g_gameplaySendReports.fetch_add(1, std::memory_order_relaxed)
               >= kMaximumGameplaySendReports) {
        return;
    }
    sockaddr_in local{};
    int localLength = static_cast<int>(sizeof(local));
    const bool hasLocal =
        ::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &localLength) != SOCKET_ERROR;
    std::array<char, 384> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=egress stage=gameplay_send api=%s socket=%llu local=0x%08X:%u "
        "remote=0x%08X:%u bytes=%zu b0=0x%02X result=%d error=%d",
        api,
        static_cast<unsigned long long>(socket),
        hasLocal ? static_cast<unsigned>(ntohl(local.sin_addr.s_addr)) : 0U,
        hasLocal ? static_cast<unsigned>(ntohs(local.sin_port)) : 0U,
        static_cast<unsigned>(ntohl(peer.sin_addr.s_addr)),
        static_cast<unsigned>(ntohs(peer.sin_port)),
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

} // namespace

/** Sends contiguous bytes to a connected exact IPv4 redirect target. */
int WSAAPI send_bytes(SOCKET socket, const char* buffer, int length, int flags) noexcept {
    const auto call = original<decltype(&::send)>(HookSlot::send);
    const bool targetsRedirect = policy::has_redirect_target_peer(socket);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    const int result = call(socket, buffer, length, flags);
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    report_gameplay_send("send",
                         socket,
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

/** Sends buffers to a connected exact IPv4 redirect target. */
int WSAAPI send_buffers(SOCKET socket,
                        LPWSABUF buffers,
                        DWORD bufferCount,
                        LPDWORD bytesSent,
                        DWORD flags,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept {
    const auto call = original<decltype(&::WSASend)>(HookSlot::wsaSend);
    const bool targetsRedirect = policy::has_redirect_target_peer(socket);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        clear_bytes(bytesSent);
        return policy::deny_socket_call();
    }
    const int result =
        call(socket, buffers, bufferCount, bytesSent, flags, overlapped, completion);
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    std::size_t bytes = 0;
    unsigned firstByte = 0;
    for (DWORD index = 0; buffers != nullptr && index < bufferCount; ++index) {
        bytes += buffers[index].len;
        if (firstByte == 0 && buffers[index].buf != nullptr && buffers[index].len != 0) {
            firstByte =
                static_cast<unsigned>(static_cast<unsigned char>(buffers[index].buf[0]));
        }
    }
    report_gameplay_send("WSASend", socket, bytes, firstByte, result, error);
    if (result == SOCKET_ERROR) {
        WSASetLastError(error);
    }
    return result;
}

/** Sends disconnect data to a connected exact IPv4 redirect target. */
int WSAAPI send_disconnect(SOCKET socket, LPWSABUF outboundData) noexcept {
    const auto call = original<decltype(&::WSASendDisconnect)>(HookSlot::wsaSendDisconnect);
    const bool targetsRedirect = policy::has_redirect_target_peer(socket);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::send, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    return call(socket, outboundData);
}

} // namespace dawn::client::hooks::egress::winsock::transmission
