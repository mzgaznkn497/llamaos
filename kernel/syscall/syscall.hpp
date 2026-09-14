#pragma once

#include "syscall_types.hpp"
#include "syscall_abi.hpp"

// =============================================================================
// LlamaOS/A - Phase 6 System Call Subsystem Manager
// =============================================================================
// Configures AMD64 SYSCALL/SYSRET MSRs (STAR, LSTAR, SFMASK, EFER.SCE),
// registers the assembly entry routine, and coordinates syscall dispatching.
// =============================================================================

namespace llamaos::syscall {

class SyscallManager {
public:
    // Initializes SYSCALL/SYSRET CPU MSRs and activates the syscall subsystem
    static void init();

    // Verifies that all SYSCALL MSRs are correctly configured in the CPU
    static bool verify();

    // Main entry point for syscall execution, invoked from assembly
    static int64_t dispatch(SyscallFrame* frame);

    // Telemetry and status queries
    static bool is_initialized() noexcept { return s_initialized; }
    static uint64_t syscall_count() noexcept { return s_syscall_count; }
    static uint64_t error_count() noexcept { return s_error_count; }

private:
    static bool     s_initialized;
    static uint64_t s_syscall_count;
    static uint64_t s_error_count;
};

} // namespace llamaos::syscall

// Low-level assembly symbol declarations
extern "C" {
    void syscall_entry();
    int64_t syscall_dispatch(llamaos::syscall::SyscallFrame* frame);
}
