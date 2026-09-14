#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Phase 6 System Call Types and Register Frame Definitions
// =============================================================================
// Defines canonical syscall numbers, status codes, and the 128-byte 16-byte
// aligned SyscallFrame capturing CPU register state across the SYSCALL boundary.
// =============================================================================

namespace llamaos::syscall {

// Canonical Syscall Numbers
enum SyscallNumber : uint64_t {
    SysInvalid    = 0,
    SysWriteDebug = 1, // Write debug string to serial log: (const char* buf, size_t len)
    SysYield      = 2, // Cooperatively yield CPU: ()
    SysGetPid     = 3, // Get current thread/process ID: ()
    SysGetTicks      = 4, // Get system timer tick count: ()
    SysMaxDefined    = 4, // Phase 6 baseline maximum
    SysExit          = 5, // Terminate user process: (int64_t code)
    SysMaxPhase7     = 5, // Phase 7 userland baseline maximum
    SysRead          = 6, // Read from file descriptor/stdin: (int fd, void* buf, size_t count)
    SysWrite         = 7, // Write to file descriptor/stdout: (int fd, const void* buf, size_t count)
    SysOpen          = 8, // Open file: (const char* path, int flags)
    SysClose         = 9, // Close file descriptor: (int fd)
    SysReaddir       = 10,// Read directory entry: (int fd, uint32_t index, DirEntry* entry)
    SysStat          = 11,// File stat: (const char* path, FileStat* st)
    SysMkdir         = 12,// Create directory: (const char* path)
    SysUnlink        = 13,// Delete file: (const char* path)
    SysReboot        = 14,// System reboot: ()
    SysPoweroff      = 15,// System poweroff: ()
    SysMemInfo       = 16,// Query memory telemetry: (uint64_t* total, uint64_t* free)
    SysPs            = 17,// Query process table: (ProcessTelemetry* buf, size_t max_count)
    SysMaxProduction = 17 // Production maximum
};

// Process telemetry entry returned by SysPs
struct [[gnu::packed]] ProcessTelemetry {
    uint32_t pid{0};
    uint32_t ppid{0};
    uint32_t state{0}; // 0: Unused, 1: Created, 2: Running, 3: Terminated
    uint32_t pad{0};
    char     name[32]{0};
    uint64_t cpu_ticks{0};
    uint64_t memory_bytes{0};
};

// Canonical Error & Return Codes
inline constexpr int64_t SYS_SUCCESS         =  0; // Operation succeeded
inline constexpr int64_t SYS_ERR_NOSYS       = -1; // Unknown or unimplemented syscall
inline constexpr int64_t SYS_ERR_INVAL       = -2; // Invalid argument supplied
inline constexpr int64_t SYS_ERR_FAULT       = -3; // Bad virtual address or null pointer
inline constexpr int64_t SYS_ERR_PERM        = -4; // Operation not permitted
inline constexpr int64_t SYS_ERR_BUSY        = -5; // Resource busy / retry needed

// Maximum length for SYS_write_debug buffer to prevent unbounded kernel loops
inline constexpr size_t SYSCALL_MAX_DEBUG_WRITE_LEN = 512;

// SyscallFrame: Exactly 128 bytes, 16-byte aligned.
// Represents the CPU state pushed by syscall_entry.asm upon executing SYSCALL.
// Distinguishable from InterruptFrame (176 bytes) and ThreadContext (80 bytes).
struct [[gnu::packed, gnu::aligned(16)]] SyscallFrame {
    // Callee-saved & argument registers preserved by entry assembly:
    uint64_t r15{0};       // Offset 0x00 (+0)
    uint64_t r14{0};       // Offset 0x08 (+8)
    uint64_t r13{0};       // Offset 0x10 (+16)
    uint64_t r12{0};       // Offset 0x18 (+24)
    uint64_t rbp{0};       // Offset 0x20 (+32)
    uint64_t rbx{0};       // Offset 0x28 (+40)
    uint64_t r9{0};        // Offset 0x30 (+48): Argument 5
    uint64_t r8{0};        // Offset 0x38 (+56): Argument 4
    uint64_t r10{0};       // Offset 0x40 (+64): Argument 3 (replaces RCX in AMD64 syscall ABI)
    uint64_t rdx{0};       // Offset 0x48 (+72): Argument 2
    uint64_t rsi{0};       // Offset 0x50 (+80): Argument 1
    uint64_t rdi{0};       // Offset 0x58 (+88): Argument 0
    uint64_t rax{0};       // Offset 0x60 (+96): Syscall number on entry, return value on exit

    // CPU hardware-captured registers on SYSCALL instruction:
    uint64_t rcx{0};       // Offset 0x68 (+104): Caller RIP saved by hardware in RCX
    uint64_t r11{0};       // Offset 0x70 (+112): Caller RFLAGS saved by hardware in R11
    uint64_t rsp{0};       // Offset 0x78 (+120): Caller RSP saved by entry assembly
};

static_assert(sizeof(SyscallFrame) == 128, "SyscallFrame must be exactly 128 bytes");
static_assert(alignof(SyscallFrame) == 16, "SyscallFrame must be 16-byte aligned");

} // namespace llamaos::syscall
