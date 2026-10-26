/**
 * Names the code a silent freeze blocks on.
 * Some freezes stop the game's own watchdog with the rest of the game, so no hitch snapshot
 * ever names the blocked code. This watcher polls the pump call count from its own thread;
 * when the count stops moving it captures every thread's rip and in-image return addresses.
 *
 * Turn on: `"stall_trace": true` in the `client` section of Dawn\settings.json.
 * Output: `ev=stall_probe` lines in Dawn\logs\dawn.log. `result=detected` marks a pump still for
 * 10 seconds, `result=watchdog_reporting` a stall the watchdog is already tracing, and
 * `result=recovered` the pump moving again; these are warn lines. Each dump writes `set=stack`
 * and `set=frames` info lines for every thread, which need `core.logging.levels.client` at
 * `info` or `debug`.
 * Limits: nothing is watched until the game has pumped 100 times, so the long pauses of boot are
 * not read as freezes. No dump while the watchdog has reported within the last 15 seconds; three
 * dumps per session, 15 seconds apart. Quitting stops the pump too, so an exit that takes longer
 * than 10 seconds writes one dump after the game leaves its session.
 */

#include "stall_probe.h"

#include <Windows.h>

#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "../../../../core/logging/log.h"
#include "../../../diagnostics/module_range.h"
#include "../../../memory/current_process_memory.h"
#include "../../../process/freeze/client_process_freeze.h"
#include "../hitch_probe/hitch_probe.h"
#include "../net_tick_probe/net_tick_probe.h"
#include "../stall_trace/stall_trace.h"

namespace dawn::client::hooks::probes::stall_probe {
namespace {

/** Times the game has called the pump, counted at its export whether or not the probe runs. */
std::atomic<std::uint64_t> g_pumpCalls{0};

/**
 * Poll cadence, and how long the count must sit still before a dump. Long enough that a normal
 * exit, which also stops the pump, usually finishes first.
 */
constexpr DWORD kPollMs = 500;
constexpr std::uint64_t kStallMs = 10'000;
/**
 * Pump calls before a stall counts. Boot pumps a couple of times and then loads for half a
 * minute; in a running session the pump turns several times a second.
 */
constexpr std::uint64_t kArmedAfterCalls = 100;
/** Repeat dumps of one long stall, spaced and capped per process: rip movement separates spin
    from wait. */
constexpr std::uint64_t kRedumpMs = 15'000;
constexpr unsigned kMaxDumps = 3;
/**
 * The watchdog repeats its report many times a second while a stall lasts. The hitch probe sees
 * every report; the stall trace sees those the assert log keeps, a few seconds apart. Silence
 * this long from both means the watchdog has stopped too.
 */
constexpr std::uint64_t kWatchdogQuietMs = 15'000;
/** Threads captured per dump; the game runs far fewer. */
constexpr std::size_t kThreadCapacity = 512;
/** Bytes of stack copied per thread, halved until a read succeeds. */
constexpr std::size_t kStackWindowBytes = 2048;
/** Module-relative return addresses kept from that window, and how many share one line. */
constexpr std::size_t kFrameLimit = 16;
constexpr std::size_t kFramesPerLine = 8;
/** Time the stopping side waits for the watcher to leave its loop. */
constexpr DWORD kJoinMs = 2'000;
/** No image maps below the first 64 KiB; a stack slot holding less is data, not a return. */
constexpr std::uint64_t kLowestCodeAddress = 0x10000;

HANDLE g_thread{};
HANDLE g_stop{};
std::atomic_bool g_installed{false};
diagnostics::ModuleRange g_gameRange{};
diagnostics::ModuleRange g_ownRange{};

/** Resolves the game image range and this DLL's own range once. */
void resolve_module_ranges() noexcept {
    (void)diagnostics::module_range(GetModuleHandleW(nullptr), g_gameRange);
    HMODULE own = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&resolve_module_ranges),
                       &own);
    (void)diagnostics::module_range(own, g_ownRange);
}

/**
 * Names the loaded module holding one address, as "<basename>+0x<rva>".
 * Runs only after every suspended thread has resumed: the loader lock may be held by one of
 * them, and this takes it.
 * @return True when a module owns the address and the token fit.
 */
[[nodiscard]] bool
format_module_address(std::uint64_t value, char* out, std::size_t size) noexcept {
    HMODULE module = nullptr;
    diagnostics::ModuleRange range{};
    if (value < kLowestCodeAddress
        || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(static_cast<std::uintptr_t>(value)),
                              &module)
               == 0
        || !diagnostics::module_range(module, range) || !diagnostics::contains(range, value)) {
        return false;
    }
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return false;
    }
    std::size_t start = length;
    while (start > 0 && path[start - 1] != L'\\' && path[start - 1] != L'/') {
        --start;
    }
    // The extension carries nothing the module name does not, and the line has a budget.
    std::size_t stop = length;
    if (stop > start + 4 && path[stop - 4] == L'.') {
        stop -= 4;
    }
    const int written = std::snprintf(
        out,
        size,
        "%.*ls+0x%llX",
        static_cast<int>(stop - start),
        path.data() + start,
        static_cast<unsigned long long>(value - reinterpret_cast<std::uintptr_t>(module)));
    return written > 0 && static_cast<std::size_t>(written) < size;
}

/**
 * Formats one code address as a module-relative token: the game image, this DLL, any other
 * loaded module by name, or raw hex when nothing owns it.
 */
void format_address(std::uint64_t value, char* out, std::size_t size) noexcept {
    if (diagnostics::contains(g_gameRange, value)) {
        std::snprintf(
            out, size, "exe+0x%llX", static_cast<unsigned long long>(value - g_gameRange.base));
        return;
    }
    if (diagnostics::contains(g_ownRange, value)) {
        std::snprintf(
            out, size, "dawn+0x%llX", static_cast<unsigned long long>(value - g_ownRange.base));
        return;
    }
    if (format_module_address(value, out, size)) {
        return;
    }
    std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(value));
}

/** @return True when some loaded image, not only the two known ones, holds the address. */
[[nodiscard]] bool in_any_module(std::uint64_t value) noexcept {
    if (diagnostics::contains(g_gameRange, value) || diagnostics::contains(g_ownRange, value)) {
        return true;
    }
    HMODULE module = nullptr;
    diagnostics::ModuleRange range{};
    // Wine answers the main image for a null address, so the range check is what decides.
    return value >= kLowestCodeAddress
           && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                     | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(static_cast<std::uintptr_t>(value)),
                                 &module)
                  != 0
           && diagnostics::module_range(module, range) && diagnostics::contains(range, value);
}

/**
 * Suspends one thread, copies its control registers and a stack window, then resumes it.
 * Only memory is taken while the thread is suspended; all logging happens after the resume.
 * @return True when the context was captured. A short or absent stack window is still success.
 */
[[nodiscard]] bool capture_thread(std::uint32_t tid,
                                  CONTEXT& context,
                                  std::byte* stack,
                                  std::size_t& stackBytes) noexcept {
    stackBytes = 0;
    const HANDLE thread = OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (thread == nullptr) {
        return false;
    }
    bool captured = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        context.ContextFlags = CONTEXT_CONTROL;
        captured = GetThreadContext(thread, &context) != 0;
        if (captured) {
            for (std::size_t size = kStackWindowBytes; size >= 256; size /= 2) {
                if (memory::read_current_process(nullptr, context.Rsp, std::span(stack, size))) {
                    stackBytes = size;
                    break;
                }
            }
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return captured;
}

/** One thread's registers and stack window, copied while it was suspended. */
struct Captured final {
    std::uint32_t tid{};
    std::uint64_t rip{};
    std::uint64_t rsp{};
    std::size_t stackBytes{};
    std::array<std::byte, kStackWindowBytes> stack{};
};

/** Logs one captured thread's rip, rsp and every module-owned return address on its stack. */
void report_thread(const Captured& thread) noexcept {
    const std::uint32_t tid = thread.tid;
    const std::size_t stackBytes = thread.stackBytes;
    const auto& stack = thread.stack;
    std::array<char, 64> ripText{};
    format_address(thread.rip, ripText.data(), ripText.size());
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=stall_probe set=stack tid=0x%08X rip=%s rsp=0x%llX "
                                "window=%zu",
                                tid,
                                ripText.data(),
                                static_cast<unsigned long long>(thread.rsp),
                                stackBytes);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    std::size_t frames = 0;
    std::size_t onLine = 0;
    std::size_t offset = 0;
    for (std::size_t index = 0; index * 8 + 8 <= stackBytes && frames < kFrameLimit; ++index) {
        std::uint64_t value = 0;
        std::memcpy(&value, stack.data() + index * 8, sizeof value);
        if (!in_any_module(value)) {
            continue;
        }
        if (onLine == 0) {
            const int prefix = std::snprintf(
                line.data(), line.size(), "ev=stall_probe set=frames tid=0x%08X", tid);
            offset = prefix > 0 ? static_cast<std::size_t>(prefix) : 0;
        }
        std::array<char, 64> text{};
        format_address(value, text.data(), text.size());
        const int piece = std::snprintf(
            line.data() + offset, line.size() - offset, " f%zu=%s", frames, text.data());
        if (piece > 0) {
            offset += static_cast<std::size_t>(piece);
        }
        ++frames;
        ++onLine;
        if (onLine == kFramesPerLine || frames == kFrameLimit) {
            core::log::write(
                core::log::Channel::client, core::log::Level::info, {line.data(), offset});
            onLine = 0;
        }
    }
    if (onLine != 0) {
        core::log::write(core::log::Channel::client, core::log::Level::info, {line.data(), offset});
    }
}

/**
 * Captures every other thread in the process, one at a time, as the only suspender, then logs
 * them. Naming a module takes the loader lock, which a frozen thread may hold, so nothing is
 * named until the suspender lock is released: a blocked lookup then stalls only this watcher,
 * never a hook install or removal.
 */
void dump_all_threads() noexcept {
    // Thread ids first, so no Toolhelp call runs while a thread is suspended.
    std::array<std::uint32_t, kThreadCapacity> ids{};
    std::size_t count = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    for (BOOL available = Thread32First(snapshot, &entry); available != FALSE && count < ids.size();
         available = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId) {
            ids[count++] = entry.th32ThreadID;
        }
    }
    CloseHandle(snapshot);
    if (count == 0) {
        return;
    }
    // Pages straight from the system, not the heap: a frozen thread may hold the heap lock.
    void* const memory = VirtualAlloc(nullptr, count * sizeof(Captured), MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (memory == nullptr) {
        return;
    }
    auto* const captures = static_cast<Captured*>(memory);
    std::size_t taken = 0;
    // A Detours transaction suspends threads too; two suspenders at once freeze each other.
    process::freeze::enter_exclusive();
    for (std::size_t index = 0; index < count; ++index) {
        alignas(16) CONTEXT context{};
        Captured* const thread = ::new (static_cast<void*>(captures + taken)) Captured{};
        thread->tid = ids[index];
        if (capture_thread(ids[index], context, thread->stack.data(), thread->stackBytes)) {
            thread->rip = context.Rip;
            thread->rsp = context.Rsp;
            ++taken;
        }
    }
    process::freeze::leave_exclusive();
    for (std::size_t index = 0; index < taken; ++index) {
        report_thread(captures[index]);
    }
    (void)VirtualFree(memory, 0, MEM_RELEASE);
}
/**
 * Writes the one-line stall marker.
 * @param result "detected", "watchdog_reporting" or "recovered".
 */
void report_marker(const char* result, std::uint64_t idleMs, std::uint64_t count) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=stall_probe result=%s idle=%llu count=%llu",
                                      result,
                                      static_cast<unsigned long long>(idleMs),
                                      static_cast<unsigned long long>(count));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** The watcher loop: poll the pump count, dump when it sits still, and sample the tick latch. */
DWORD WINAPI watch(LPVOID) noexcept {
    std::uint64_t lastCount = pump_count();
    std::uint64_t lastMoveMs = GetTickCount64();
    std::uint64_t nextDumpMs = 0;
    unsigned dumps = 0;
    bool stalled = false;
    bool deferred = false;
    while (WaitForSingleObject(g_stop, kPollMs) == WAIT_TIMEOUT) {
        // Sampled from here rather than the pump, so the latch is still read once the pump stops.
        net_tick_probe::sample();
        const std::uint64_t count = pump_count();
        const std::uint64_t now = GetTickCount64();
        if (count != lastCount) {
            if (stalled) {
                report_marker("recovered", now - lastMoveMs, count);
                stalled = deferred = false;
            }
            lastCount = count;
            lastMoveMs = now;
            continue;
        }
        // Boot pumps a few times and then pauses to load, which is not the freeze this is for.
        if (count < kArmedAfterCalls || now - lastMoveMs < kStallMs) {
            continue;
        }
        if (!stalled) {
            report_marker("detected", now - lastMoveMs, count);
            stalled = true;
        }
        // A stall the watchdog is still reporting is already traced, stalled job included, by the
        // stall trace and the hitch probe. This dump is for the freeze that silences the watchdog.
        const std::uint64_t hitch =
            (std::max)(hitch_probe::last_report_tick(), stall_trace::last_hitch_tick());
        if (hitch >= lastMoveMs && now - hitch < kWatchdogQuietMs) {
            if (!deferred) {
                report_marker("watchdog_reporting", now - lastMoveMs, count);
                deferred = true;
            }
            continue;
        }
        if (now < nextDumpMs || dumps >= kMaxDumps) {
            continue;
        }
        dump_all_threads();
        ++dumps;
        nextDumpMs = now + kRedumpMs;
    }
    return 0;
}

} // namespace

void note_pump() noexcept {
    g_pumpCalls.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t pump_count() noexcept {
    return g_pumpCalls.load(std::memory_order_relaxed);
}

/** Starts the stall watcher thread. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    resolve_module_ranges();
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop == nullptr) {
        return false;
    }
    g_thread = CreateThread(nullptr, 0, &watch, nullptr, 0, nullptr);
    if (g_thread == nullptr) {
        CloseHandle(g_stop);
        g_stop = nullptr;
        return false;
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=stall_probe result=installed");
    return true;
}

/** Stops the watcher thread. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    SetEvent(g_stop);
    // A watcher still inside a dump keeps its event and its handle; closing either under it is
    // undefined, and the caller must not unload code the watcher is still running.
    if (WaitForSingleObject(g_thread, kJoinMs) != WAIT_OBJECT_0) {
        return false;
    }
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = nullptr;
    g_stop = nullptr;
    g_installed.store(false, std::memory_order_release);
    return true;
}

} // namespace dawn::client::hooks::probes::stall_probe
