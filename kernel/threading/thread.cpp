#include "thread.hpp"

// =============================================================================
// LlamaOS/A - Thread Control Block Implementation
// =============================================================================

namespace llamaos::threading {

void init_thread_context(ThreadControlBlock* tcb,
                         ThreadEntry entry,
                         void* argument,
                         uintptr_t trampoline_addr) {
    if (!tcb) return;

    tcb->entry = entry;
    tcb->argument = argument;

    // Stack top must be 16-byte aligned per System V AMD64 ABI
    uint64_t rsp_val = tcb->stack.stack_top.value();
    rsp_val &= ~0xFULL; // Enforce 16-byte alignment

    tcb->context.r15 = 0;
    tcb->context.r14 = 0;
    tcb->context.r13 = reinterpret_cast<uint64_t>(argument); // Arg placed into r13 for trampoline
    tcb->context.r12 = reinterpret_cast<uint64_t>(entry);    // Entry placed into r12 for trampoline
    tcb->context.rbp = 0;
    tcb->context.rbx = 0;
    tcb->context.rflags = 0x202; // IF=1, reserved bit 1=1
    tcb->context.rsp = rsp_val;
    tcb->context.rip = trampoline_addr;
}

} // namespace llamaos::threading
