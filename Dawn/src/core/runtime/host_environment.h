#pragma once

namespace dawn::core::runtime {

/**
 * Reports whether the process runs under Wine instead of Windows.
 * @return True when the loaded ntdll exports Wine's own version entry point.
 */
[[nodiscard]] bool is_wine() noexcept;
} // namespace dawn::core::runtime
