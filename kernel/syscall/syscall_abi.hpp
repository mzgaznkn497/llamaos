#pragma once

#include "syscall_types.hpp"

// =============================================================================
// LlamaOS/A - Phase 6 System Call ABI Specification
// =============================================================================
// Establishes the 64-bit AMD64 System Call Application Binary Interface (ABI):
//
// 1. Invocation Mechanism:
//    - Userland/Caller executes: `syscall`
//    - Kernel returns via:       `sysretq` (Ring 3) or safe frame return (Ring 0)
//
// 2. Register Conventions:
//    - Syscall Number:   RAX
//    - Argument 0:       RDI
//    - Argument 1:       RSI
//    - Argument 2:       RDX
//    - Argument 3:       R10 (Note: AMD64 SYSCALL clobbers RCX with return RIP,
//                             so R10 replaces RCX as the 4th argument)
//    - Argument 4:       R8
//    - Argument 5:       R9
//    - Return Value:     RAX (non-negative for success, negative for error)
//
// 3. Hardware State Transfer on SYSCALL:
//    - RCX <- Return RIP
//    - R11 <- Return RFLAGS
//    - RFLAGS <- RFLAGS & ~IA32_FMASK (IF=0, DF=0, TF=0, NT=0, AC=0)
//    - CS  <- STAR[47:32] & ~3 (0x0008, Kernel Code)
//    - SS  <- (STAR[47:32] & ~3) + 8 (0x0010, Kernel Data)
//    - RIP <- IA32_LSTAR (syscall_entry virtual address)
//    - RSP remains unchanged by hardware (switched explicitly by assembly)
//
// 4. Hardware State Transfer on SYSRETQ:
//    - RIP <- RCX
//    - RFLAGS <- (R11 & 0x3C7FD7) | 0x02
//    - CS  <- (STAR[63:48] + 16) | 3 (0x0033, User Code DPL 3)
//    - SS  <- (STAR[63:48] + 8)  | 3 (0x002B, User Data DPL 3)
//    - CPL transitions to Ring 3
//
// 5. Preserved Registers:
//    - Callee-saved: RBX, RBP, R12, R13, R14, R15, RSP
// =============================================================================

namespace llamaos::syscall {

// Architectural MSR Constants for SYSCALL/SYSRET
// STAR: Bits [47:32] = Kernel CS/SS base (0x0008), Bits [63:48] = User CS/SS base (0x0020)
inline constexpr uint64_t SYSCALL_STAR_KERNEL_BASE = 0x0008ULL;
inline constexpr uint64_t SYSCALL_STAR_USER_BASE   = 0x0020ULL;
inline constexpr uint64_t SYSCALL_STAR_VALUE       = (SYSCALL_STAR_USER_BASE << 48) |
                                                     (SYSCALL_STAR_KERNEL_BASE << 32);

// SFMASK: Masks IF (0x200), DF (0x400), TF (0x100), NT (0x4000), AC (0x40000)
// Ensures deterministic entry state with interrupts disabled and direction flag cleared.
inline constexpr uint64_t SYSCALL_SFMASK_VALUE     = 0x44700ULL;

// EFER.SCE: System Call Extensions enable bit (bit 0 of IA32_EFER)
inline constexpr uint64_t EFER_SCE_BIT             = (1ULL << 0);

// Host-testable helper to trigger a 64-bit system call from freestanding code
inline int64_t trigger_syscall(uint64_t num,
                               uint64_t arg0 = 0,
                               uint64_t arg1 = 0,
                               uint64_t arg2 = 0,
                               uint64_t arg3 = 0,
                               uint64_t arg4 = 0,
                               uint64_t arg5 = 0) {
    int64_t ret;
    register uint64_t r10 asm("r10") = arg3;
    register uint64_t r8  asm("r8")  = arg4;
    register uint64_t r9  asm("r9")  = arg5;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg0), "S"(arg1), "d"(arg2), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );

    return ret;
}

} // namespace llamaos::syscall
