#pragma once

#include "../platform/sdk.h"

namespace dawn::client::hooks::egress::extensions {
/** Transmits file data only to a connected exact IPv4 redirect target. */
BOOL PASCAL transmit_file(SOCKET socket,
                          HANDLE file,
                          DWORD bytesToWrite,
                          DWORD bytesPerSend,
                          LPOVERLAPPED overlapped,
                          LPTRANSMIT_FILE_BUFFERS buffers,
                          DWORD reserved) noexcept;

} // namespace dawn::client::hooks::egress::extensions
