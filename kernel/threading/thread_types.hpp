#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Process & Threading Subsystem Types
// =============================================================================
// Defines strongly-typed thread states, thread identifiers, priorities,
// and execution function signatures for Ring 0 kernel threads.
// =============================================================================

namespace llamaos::threading {

// Strongly-typed thread execution states
enum class ThreadState : uint8_t {
    Created,    // TCB initialized, initial context prepared, not yet in ready queue
    Ready,      // Present in ready queue, eligible for CPU scheduling
    Running,    // Currently executing on CPU
    Blocked,    // Suspended waiting for an event/condition
    Terminated, // Execution completed, awaiting deferred stack reclamation
    Idle        // Dedicated low-power system idle thread
};

[[nodiscard]] constexpr const char* to_string(ThreadState state) noexcept {
    switch (state) {
        case ThreadState::Created:    return "Created";
        case ThreadState::Ready:      return "Ready";
        case ThreadState::Running:    return "Running";
        case ThreadState::Blocked:    return "Blocked";
        case ThreadState::Terminated: return "Terminated";
        case ThreadState::Idle:       return "Idle";
        default:                      return "Unknown";
    }
}

// Thread scheduling priority levels
enum class ThreadPriority : uint8_t {
    Idle     = 0,
    Low      = 1,
    Normal   = 2,
    High     = 3,
    Realtime = 4
};

// Strongly-typed thread identifier
using ThreadId = uint32_t;

inline constexpr ThreadId INVALID_THREAD_ID   = 0;
inline constexpr ThreadId BOOTSTRAP_THREAD_ID = 1;
inline constexpr ThreadId IDLE_THREAD_ID      = 2;

// Default round-robin timeslice: 2 ticks = 20 ms at 100 Hz
inline constexpr uint64_t DEFAULT_TIMESLICE_TICKS = 2;

// Maximum length of thread descriptive name (including null terminator)
inline constexpr size_t THREAD_NAME_MAX_LEN = 32;

// Kernel thread entry point function prototype
using ThreadEntry = void (*)(void*);

} // namespace llamaos::threading
