#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../memory/current_process_memory.h"

namespace dawn::client::hooks::probes::effect_trace {

/** A handle slot that names nothing. */
inline constexpr std::uint32_t kNone = 0xFFFFFFFFU;
/** Longest line the trace queues; longer lines are cut, never split. */
inline constexpr std::size_t kLineCapacity = 512;

/**
 * Queues one line for the trace's writer thread, which hands it to the Dawn log. The hooked game
 * thread never waits on the disk. Lines queued while the trace is not installed are dropped.
 */
void emit(std::string_view line) noexcept;
/** Formats one line and queues it. */
template <typename... Args> void line(const char* format, Args... args) noexcept {
    std::array<char, kLineCapacity> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), format, args...);
    if (length > 0) {
        emit({buffer.data(), (std::min)(static_cast<std::size_t>(length), buffer.size() - 1)});
    }
}

/** Reads one live game value; false when the address is outside user space or unreadable. */
template <typename Value> bool read(std::uintptr_t address, Value& value) noexcept {
    return address >= 0x10000 && address < 0x0000800000000000ULL - sizeof(Value)
        && memory::read_current_process(nullptr, address, std::as_writable_bytes(std::span(&value, 1)));
}

/** Kind-3 perk spawn requests. */
[[nodiscard]] bool install_spawn() noexcept;
[[nodiscard]] bool uninstall_spawn() noexcept;
/**
 * Writer thread, or the stopping thread once the spawn hook is gone: writes the count each
 * graph's budget held back once its interval has ended, and frees that graph's slot.
 * @param closing Also ends every interval still running, as the trace stops.
 */
void service_spawn_budgets(std::uint64_t now, bool closing) noexcept;

/** Every perk effect applied or removed, whatever its kind. */
[[nodiscard]] bool install_effects() noexcept;
[[nodiscard]] bool uninstall_effects() noexcept;
/** As `service_spawn_budgets`, for the effect budgets. */
void service_effect_budgets(std::uint64_t now, bool closing) noexcept;

/** Weapon fire, projectile launches and their flight, and ammo changes. */
[[nodiscard]] bool install_projectiles() noexcept;
[[nodiscard]] bool uninstall_projectiles() noexcept;
/** Writer thread only: takes each followed projectile's samples as they come due. */
void service_flights(std::uint64_t now) noexcept;
/** Reports every projectile still being followed, as it stands. */
void flush_flights() noexcept;

} // namespace dawn::client::hooks::probes::effect_trace
