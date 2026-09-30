#include <array>
#include <atomic>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../core/settings/settings.h"
#include "egress_connection_replacements.h"

#include "../../internal.h"
#include "../../policy/policy.h"
#include "../../resolver/redirect.h"

namespace dawn::client::hooks::egress::winsock::connection {
namespace {

/** WSAConnectByList could otherwise pick an endpoint off the redirect target. */
constexpr INT kAllowedAddressCount = 1;
/** Direct gameplay connection attempts retained before later retries are suppressed. */
constexpr unsigned kMaximumGameplayConnectReports = 16;
/** Retail's historical port identifies stale activity-host descriptors. */
constexpr std::uint16_t kRetailGameplayPort = 30976;

std::atomic<unsigned> g_gameplayConnectReports{0};

/** Reports the tuple selected by the redirect immediately after one gameplay connect call. */
void report_gameplay_connect(const char* api,
                             SOCKET socket,
                             const sockaddr_in& remote,
                             int result,
                             int error) noexcept {
    const std::uint16_t port = ntohs(remote.sin_port);
    if ((port != kRetailGameplayPort && port != core::settings::get().server.gameplay.port)
        || g_gameplayConnectReports.fetch_add(1, std::memory_order_relaxed)
               >= kMaximumGameplayConnectReports) {
        return;
    }
    sockaddr_in local{};
    int localLength = static_cast<int>(sizeof(local));
    const bool hasLocal =
        ::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &localLength) != SOCKET_ERROR;
    std::array<char, 352> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=egress stage=gameplay_connect api=%s socket=%llu local=0x%08X:%u "
        "remote=0x%08X:%u result=%d error=%d",
        api,
        static_cast<unsigned long long>(socket),
        hasLocal ? static_cast<unsigned>(ntohl(local.sin_addr.s_addr)) : 0U,
        hasLocal ? static_cast<unsigned>(ntohs(local.sin_port)) : 0U,
        static_cast<unsigned>(ntohl(remote.sin_addr.s_addr)),
        static_cast<unsigned>(port),
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

/** Redirects one complete IPv4 destination to the exact redirect target. */
int WSAAPI connect_socket(SOCKET socket, const sockaddr* name, int nameLength) noexcept {
    const auto call = original<decltype(&::connect)>(HookSlot::connect);
    sockaddr_in redirected{};
    const bool targetsRedirect = policy::redirect_ipv4(name, nameLength, redirected);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    const int result = call(socket,
                            reinterpret_cast<const sockaddr*>(&redirected),
                            static_cast<int>(sizeof(redirected)));
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    report_gameplay_connect("connect", socket, redirected, result, error);
    if (result == SOCKET_ERROR) {
        WSASetLastError(error);
    }
    return result;
}

/** Redirects an IPv4 connection with caller and quality-of-service data to the redirect target. */
int WSAAPI connect_socket_ex(SOCKET socket,
                             const sockaddr* name,
                             int nameLength,
                             LPWSABUF callerData,
                             LPWSABUF calleeData,
                             LPQOS socketQos,
                             LPQOS groupQos) noexcept {
    const auto call = original<decltype(&::WSAConnect)>(HookSlot::wsaConnect);
    sockaddr_in redirected{};
    const bool targetsRedirect = policy::redirect_ipv4(name, nameLength, redirected);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        return policy::deny_socket_call();
    }
    const int result = call(socket,
                            reinterpret_cast<const sockaddr*>(&redirected),
                            static_cast<int>(sizeof(redirected)),
                            callerData,
                            calleeData,
                            socketQos,
                            groupQos);
    const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
    report_gameplay_connect("WSAConnect", socket, redirected, result, error);
    if (result == SOCKET_ERROR) {
        WSASetLastError(error);
    }
    return result;
}

/** Redirects every nonempty ANSI node to the IPv4 redirect literal. */
BOOL PASCAL connect_by_name_a(SOCKET socket,
                              LPCSTR nodeName,
                              LPCSTR serviceName,
                              LPDWORD localAddressLength,
                              LPSOCKADDR localAddress,
                              LPDWORD remoteAddressLength,
                              LPSOCKADDR remoteAddress,
                              const timeval* timeout,
                              LPWSAOVERLAPPED reserved) noexcept {
    const auto call = original<decltype(&::WSAConnectByNameA)>(HookSlot::wsaConnectByNameA);
    const bool targetsRedirect = nodeName != nullptr && nodeName[0] != '\0';
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        (void)policy::deny_socket_call();
        return FALSE;
    }
    return call(socket,
                resolver::redirect_host_a(),
                serviceName,
                localAddressLength,
                localAddress,
                remoteAddressLength,
                remoteAddress,
                timeout,
                reserved);
}

/** Redirects every nonempty wide node to the IPv4 redirect literal. */
BOOL PASCAL connect_by_name_w(SOCKET socket,
                              LPWSTR nodeName,
                              LPWSTR serviceName,
                              LPDWORD localAddressLength,
                              LPSOCKADDR localAddress,
                              LPDWORD remoteAddressLength,
                              LPSOCKADDR remoteAddress,
                              const timeval* timeout,
                              LPWSAOVERLAPPED reserved) noexcept {
    const auto call = original<decltype(&::WSAConnectByNameW)>(HookSlot::wsaConnectByNameW);
    const bool targetsRedirect = nodeName != nullptr && nodeName[0] != L'\0';
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        (void)policy::deny_socket_call();
        return FALSE;
    }
    resolver::WideNode redirectedNode = resolver::redirect_node();
    return call(socket,
                redirectedNode.data(),
                serviceName,
                localAddressLength,
                localAddress,
                remoteAddressLength,
                remoteAddress,
                timeout,
                reserved);
}

/** Redirects one caller-provided IPv4 address to the exact redirect target. */
BOOL PASCAL connect_by_list(SOCKET socket,
                            PSOCKET_ADDRESS_LIST addresses,
                            LPDWORD localAddressLength,
                            LPSOCKADDR localAddress,
                            LPDWORD remoteAddressLength,
                            LPSOCKADDR remoteAddress,
                            const timeval* timeout,
                            LPWSAOVERLAPPED reserved) noexcept {
    const auto call = original<decltype(&::WSAConnectByList)>(HookSlot::wsaConnectByList);
    sockaddr_in redirectedAddress{};
    const bool hasSingleAddress =
        addresses != nullptr && addresses->iAddressCount == kAllowedAddressCount;
    const bool targetsRedirect = hasSingleAddress
                                 && policy::redirect_ipv4(addresses->Address[0].lpSockaddr,
                                                          addresses->Address[0].iSockaddrLength,
                                                          redirectedAddress);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        (void)policy::deny_socket_call();
        return FALSE;
    }

    SOCKET_ADDRESS_LIST redirectedList{};
    redirectedList.iAddressCount = kAllowedAddressCount;
    redirectedList.Address[0].lpSockaddr = reinterpret_cast<sockaddr*>(&redirectedAddress);
    redirectedList.Address[0].iSockaddrLength = static_cast<INT>(sizeof(redirectedAddress));
    return call(socket,
                &redirectedList,
                localAddressLength,
                localAddress,
                remoteAddressLength,
                remoteAddress,
                timeout,
                reserved);
}

/** Redirects one complete IPv4 multipoint destination to the exact redirect target. */
SOCKET WSAAPI join_leaf(SOCKET socket,
                        const sockaddr* name,
                        int nameLength,
                        LPWSABUF callerData,
                        LPWSABUF calleeData,
                        LPQOS socketQos,
                        LPQOS groupQos,
                        DWORD flags) noexcept {
    const auto call = original<decltype(&::WSAJoinLeaf)>(HookSlot::wsaJoinLeaf);
    sockaddr_in redirected{};
    const bool targetsRedirect = policy::redirect_ipv4(name, nameLength, redirected);
    if (call == nullptr
        || !policy::allow_socket_call(policy::SocketOperation::connect, targetsRedirect, true)) {
        (void)policy::deny_socket_call();
        return INVALID_SOCKET;
    }
    return call(socket,
                reinterpret_cast<const sockaddr*>(&redirected),
                static_cast<int>(sizeof(redirected)),
                callerData,
                calleeData,
                socketQos,
                groupQos,
                flags);
}

} // namespace dawn::client::hooks::egress::winsock::connection
