#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "threading/thread_types.hpp"

// =============================================================================
// LlamaOS/A - Thread Control Block (TCB) & Execution Context
// =============================================================================
// Defines the low-level CPU execution context (callee-saved registers for
// System V AMD64 ABI) and the Thread Control Block representing an independent
// schedulable entity in Ring 0 kernel space.
// =============================================================================

namespace llamaos::threading {

// -----------------------------------------------------------------------------
// ThreadContext: Low-level CPU Execution Context
// -----------------------------------------------------------------------------
// Preserves the System V AMD64 ABI callee-saved registers, execution flags,
// stack pointer, and instruction pointer across context switches.
// Layout must strictly match assembly offsets in context_switch.asm:
//   Offset 0x00: R15
//   Offset 0x08: R14
//   Offset 0x10: R13
//   Offset 0x18: R12
//   Offset 0x20: RBP
//   Offset 0x28: RBX
//   Offset 0x30: RFLAGS
//   Offset 0x38: RSP
//   Offset 0x40: RIP
// -----------------------------------------------------------------------------
struct alignas(16) ThreadContext {
    uint64_t r15{0};        // Offset 0x00
    uint64_t r14{0};        // Offset 0x08
    uint64_t r13{0};        // Offset 0x10
    uint64_t r12{0};        // Offset 0x18
    uint64_t rbp{0};        // Offset 0x20
    uint64_t rbx{0};        // Offset 0x28
    uint64_t rflags{0x202}; // Offset 0x30 (Bit 1 reserved=1, Bit 9 IF=1)
    uint64_t rsp{0};        // Offset 0x38
    uint64_t rip{0};        // Offset 0x40
    uint64_t reserved{0};   // Offset 0x48 (16-byte alignment padding)
};

static_assert(sizeof(ThreadContext) == 80, "ThreadContext must be 80 bytes (aligned to 16 bytes)");
static_assert(alignof(ThreadContext) == 16, "ThreadContext alignment must be 16 bytes");
static_assert(__builtin_offsetof(ThreadContext, r15) == 0, "r15 offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, r14) == 8, "r14 offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, r13) == 16, "r13 offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, r12) == 24, "r12 offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, rbp) == 32, "rbp offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, rbx) == 40, "rbx offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, rflags) == 48, "rflags offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, rsp) == 56, "rsp offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, rip) == 64, "rip offset mismatch");
static_assert(__builtin_offsetof(ThreadContext, reserved) == 72, "reserved offset mismatch");

// -----------------------------------------------------------------------------
// ThreadStackInfo: Stack Layout and Memory Boundaries
// -----------------------------------------------------------------------------
struct ThreadStackInfo {
    memory::VirtualAddress  guard_page{0};   // 4 KiB unmapped non-present boundary
    memory::VirtualAddress  stack_bottom{0}; // Virtual start of usable stack memory
    memory::VirtualAddress  stack_top{0};    // High-address boundary of usable stack
    memory::PhysicalAddress physical_base{0};// Backing physical frame allocation
    size_t                  usable_bytes{0}; // Usable size in bytes (e.g. 16 KiB)
    size_t                  page_count{0};   // Physical/virtual pages allocated
    int32_t                 slot_index{-1};  // Stack allocator slot index
};

// -----------------------------------------------------------------------------
// ThreadControlBlock (TCB): Kernel Thread Descriptor
// -----------------------------------------------------------------------------
struct ThreadControlBlock {
    ThreadId            id{INVALID_THREAD_ID};
    char                name[THREAD_NAME_MAX_LEN]{};
    ThreadState         state{ThreadState::Created};
    ThreadPriority      priority{ThreadPriority::Normal};

    ThreadContext       context{};
    ThreadStackInfo     stack{};

    ThreadEntry         entry{nullptr};
    void*               argument{nullptr};

    // Scheduling & telemetry metrics
    uint64_t            ticks_allocated{0};
    uint64_t            ticks_consumed{0};
    uint64_t            timeslice_ticks{DEFAULT_TIMESLICE_TICKS};
    uint64_t            switch_count{0};
    uint64_t            preemption_count{0};
    uint64_t            voluntary_yield_count{0};

    bool                is_idle{false};
    bool                is_bootstrap{false};
    bool                active{false};       // Slot in use in TCB pool
    uint64_t            cr3{0};              // 0 = kernel root CR3, >0 = isolated process CR3
};

// Initializes initial thread context so first context switch jumps to trampoline
void init_thread_context(ThreadControlBlock* tcb,
                         ThreadEntry entry,
                         void* argument,
                         uintptr_t trampoline_addr);

} // namespace llamaos::threading
