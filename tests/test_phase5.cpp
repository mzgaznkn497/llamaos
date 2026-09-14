#include "threading/thread_types.hpp"
#include "threading/thread.hpp"
#include "threading/ready_queue.hpp"
#include "threading/stack_allocator.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

// =============================================================================
// LlamaOS/A - Phase 5 Process & Threading Host Unit Test Suite
// =============================================================================

using namespace llamaos::threading;

static size_t g_assert_count = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] Line %d: Assertion '%s' failed: %s\n", __LINE__, #cond, msg); \
            assert(false); \
        } \
        g_assert_count++; \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: ThreadState and Priority Representations
// -----------------------------------------------------------------------------
void test_thread_state_machine() {
    printf(" [RUN]  ThreadState Transitions & Priority Enums             ...");

    ThreadControlBlock tcb{};
    tcb.state = ThreadState::Created;
    TEST_ASSERT(tcb.state == ThreadState::Created, "Initial state must be Created");
    TEST_ASSERT(strcmp(to_string(tcb.state), "Created") == 0, "String Created mismatch");

    // Transition Created -> Ready
    tcb.state = ThreadState::Ready;
    TEST_ASSERT(tcb.state == ThreadState::Ready, "Must transition to Ready");
    TEST_ASSERT(strcmp(to_string(tcb.state), "Ready") == 0, "String Ready mismatch");

    // Transition Ready -> Running
    tcb.state = ThreadState::Running;
    TEST_ASSERT(tcb.state == ThreadState::Running, "Must transition to Running");
    TEST_ASSERT(strcmp(to_string(tcb.state), "Running") == 0, "String Running mismatch");

    // Transition Running -> Blocked
    tcb.state = ThreadState::Blocked;
    TEST_ASSERT(tcb.state == ThreadState::Blocked, "Must transition to Blocked");
    TEST_ASSERT(strcmp(to_string(tcb.state), "Blocked") == 0, "String Blocked mismatch");

    // Transition Blocked -> Ready
    tcb.state = ThreadState::Ready;
    TEST_ASSERT(tcb.state == ThreadState::Ready, "Must transition Blocked -> Ready");

    // Transition Ready -> Running -> Terminated
    tcb.state = ThreadState::Running;
    tcb.state = ThreadState::Terminated;
    TEST_ASSERT(tcb.state == ThreadState::Terminated, "Must transition to Terminated");
    TEST_ASSERT(strcmp(to_string(tcb.state), "Terminated") == 0, "String Terminated mismatch");

    // Idle state
    tcb.state = ThreadState::Idle;
    TEST_ASSERT(strcmp(to_string(tcb.state), "Idle") == 0, "String Idle mismatch");

    // Priority levels
    tcb.priority = ThreadPriority::Low;
    TEST_ASSERT(static_cast<uint8_t>(tcb.priority) == 1, "Low priority level mismatch");
    tcb.priority = ThreadPriority::Normal;
    TEST_ASSERT(static_cast<uint8_t>(tcb.priority) == 2, "Normal priority level mismatch");
    tcb.priority = ThreadPriority::High;
    TEST_ASSERT(static_cast<uint8_t>(tcb.priority) == 3, "High priority level mismatch");
    tcb.priority = ThreadPriority::Realtime;
    TEST_ASSERT(static_cast<uint8_t>(tcb.priority) == 4, "Realtime priority level mismatch");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 2: CPU Execution Context Structure and Offset Guarantees
// -----------------------------------------------------------------------------
void test_context_layout() {
    printf(" [RUN]  ThreadContext Memory Layout & Assembly Offsets       ...");

    TEST_ASSERT(sizeof(ThreadContext) == 80, "ThreadContext size must be 80 bytes (aligned to 16 bytes)");
    TEST_ASSERT(alignof(ThreadContext) == 16, "ThreadContext alignment must be 16 bytes");

    TEST_ASSERT(__builtin_offsetof(ThreadContext, r15) == 0, "r15 offset must be 0x00");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, r14) == 8, "r14 offset must be 0x08");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, r13) == 16, "r13 offset must be 0x10");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, r12) == 24, "r12 offset must be 0x18");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, rbp) == 32, "rbp offset must be 0x20");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, rbx) == 40, "rbx offset must be 0x28");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, rflags) == 48, "rflags offset must be 0x30");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, rsp) == 56, "rsp offset must be 0x38");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, rip) == 64, "rip offset must be 0x40");
    TEST_ASSERT(__builtin_offsetof(ThreadContext, reserved) == 72, "reserved offset must be 0x48");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 3: Thread Context Initialization & System V AMD64 ABI Alignment
// -----------------------------------------------------------------------------
static void dummy_thread_entry(void*) {}

void test_context_initialization() {
    printf(" [RUN]  Initial Context Setup & SysV AMD64 16-Byte Stack     ...");

    ThreadControlBlock tcb{};
    // Stack top is 4096-byte aligned (e.g. 0xFFFFFFFF72005000)
    tcb.stack.stack_top = llamaos::memory::VirtualAddress(0xFFFFFFFF72005000ULL);

    uintptr_t dummy_trampoline = 0xFFFFFFFF80105000ULL;
    int test_arg = 42;

    init_thread_context(&tcb, dummy_thread_entry, &test_arg, dummy_trampoline);

    TEST_ASSERT(tcb.entry == dummy_thread_entry, "Entry point mismatch");
    TEST_ASSERT(tcb.argument == &test_arg, "Argument pointer mismatch");
    TEST_ASSERT(tcb.context.rip == dummy_trampoline, "RIP must point to trampoline");
    TEST_ASSERT(tcb.context.r12 == reinterpret_cast<uint64_t>(dummy_thread_entry), "R12 must hold entry func");
    TEST_ASSERT(tcb.context.r13 == reinterpret_cast<uint64_t>(&test_arg), "R13 must hold entry arg");
    TEST_ASSERT((tcb.context.rflags & (1ULL << 9)) != 0, "RFLAGS must have Interrupt Flag (IF) set");
    TEST_ASSERT((tcb.context.rflags & (1ULL << 1)) != 0, "RFLAGS reserved bit 1 must be set");

    // Crucial ABI invariant: RSP must be 16-byte aligned before calling C++
    TEST_ASSERT((tcb.context.rsp % 16) == 0, "Initial RSP must be strictly 16-byte aligned");
    TEST_ASSERT(tcb.context.rsp == 0xFFFFFFFF72005000ULL, "RSP must equal stack top");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 4: Ready Queue FIFO Scheduling, Invariants & Duplicate Prevention
// -----------------------------------------------------------------------------
void test_ready_queue() {
    printf(" [RUN]  ReadyQueue FIFO, Duplicate Prevention & Capacity      ...");

    ReadyQueue queue{};
    TEST_ASSERT(queue.is_empty(), "Queue must start empty");
    TEST_ASSERT(queue.size() == 0, "Size must be zero");
    TEST_ASSERT(!queue.is_full(), "Queue must not be full");
    TEST_ASSERT(queue.capacity() == 64, "Capacity must be 64");

    ThreadControlBlock tcb1{}, tcb2{}, tcb3{};
    tcb1.id = 101; tcb1.state = ThreadState::Ready;
    tcb2.id = 102; tcb2.state = ThreadState::Ready;
    tcb3.id = 103; tcb3.state = ThreadState::Ready;

    // Enqueue
    TEST_ASSERT(queue.enqueue(&tcb1), "Enqueue tcb1 should succeed");
    TEST_ASSERT(queue.size() == 1, "Size must be 1");
    TEST_ASSERT(queue.contains(&tcb1), "Queue must contain tcb1");
    TEST_ASSERT(!queue.contains(&tcb2), "Queue must not contain tcb2 yet");

    // DUPLICATE PREVENTION: Enqueuing tcb1 again MUST fail
    TEST_ASSERT(!queue.enqueue(&tcb1), "Enqueueing duplicate tcb1 must fail");
    TEST_ASSERT(queue.size() == 1, "Size must remain 1 after duplicate rejection");

    // Enqueue remaining
    TEST_ASSERT(queue.enqueue(&tcb2), "Enqueue tcb2 should succeed");
    TEST_ASSERT(queue.enqueue(&tcb3), "Enqueue tcb3 should succeed");
    TEST_ASSERT(queue.size() == 3, "Size must be 3");

    // State invariants: Enqueuing non-Ready threads MUST fail
    ThreadControlBlock blocked_tcb{};
    blocked_tcb.id = 104; blocked_tcb.state = ThreadState::Blocked;
    TEST_ASSERT(!queue.enqueue(&blocked_tcb), "Blocked thread must be rejected by ready queue");

    ThreadControlBlock terminated_tcb{};
    terminated_tcb.id = 105; terminated_tcb.state = ThreadState::Terminated;
    TEST_ASSERT(!queue.enqueue(&terminated_tcb), "Terminated thread must be rejected by ready queue");

    ThreadControlBlock running_tcb{};
    running_tcb.id = 106; running_tcb.state = ThreadState::Running;
    TEST_ASSERT(!queue.enqueue(&running_tcb), "Running thread must be rejected by ready queue");

    TEST_ASSERT(!queue.enqueue(nullptr), "Null pointer must be rejected by ready queue");

    // Dequeue FIFO ordering
    ThreadControlBlock* out1 = queue.dequeue();
    TEST_ASSERT(out1 == &tcb1, "FIFO must dequeue tcb1 first");
    TEST_ASSERT(queue.size() == 2, "Size must be 2");

    ThreadControlBlock* out2 = queue.dequeue();
    TEST_ASSERT(out2 == &tcb2, "FIFO must dequeue tcb2 second");
    TEST_ASSERT(queue.size() == 1, "Size must be 1");

    ThreadControlBlock* out3 = queue.dequeue();
    TEST_ASSERT(out3 == &tcb3, "FIFO must dequeue tcb3 third");
    TEST_ASSERT(queue.is_empty(), "Queue must now be empty");
    TEST_ASSERT(queue.dequeue() == nullptr, "Dequeue on empty queue must return nullptr");

    // Removal test
    tcb1.state = ThreadState::Ready;
    tcb2.state = ThreadState::Ready;
    tcb3.state = ThreadState::Ready;
    TEST_ASSERT(queue.enqueue(&tcb1), "Enqueue tcb1 for removal test");
    TEST_ASSERT(queue.enqueue(&tcb2), "Enqueue tcb2 for removal test");
    TEST_ASSERT(queue.enqueue(&tcb3), "Enqueue tcb3 for removal test");

    TEST_ASSERT(queue.remove(&tcb2), "Remove tcb2 should succeed");
    TEST_ASSERT(queue.size() == 2, "Size must be 2 after removal");
    TEST_ASSERT(!queue.contains(&tcb2), "Queue must not contain tcb2 after removal");
    TEST_ASSERT(queue.dequeue() == &tcb1, "Dequeue must yield tcb1");
    TEST_ASSERT(queue.dequeue() == &tcb3, "Dequeue must yield tcb3 (skipping removed tcb2)");
    TEST_ASSERT(queue.is_empty(), "Queue must be empty");

    // Fill capacity
    ThreadControlBlock pool[ReadyQueue::MAX_CAPACITY]{};
    for (size_t i = 0; i < ReadyQueue::MAX_CAPACITY; ++i) {
        pool[i].id = static_cast<ThreadId>(200 + i);
        pool[i].state = ThreadState::Ready;
        TEST_ASSERT(queue.enqueue(&pool[i]), "Enqueue up to max capacity must succeed");
    }
    TEST_ASSERT(queue.is_full(), "Queue must be full at max capacity");

    ThreadControlBlock overflow_tcb{};
    overflow_tcb.id = 999; overflow_tcb.state = ThreadState::Ready;
    TEST_ASSERT(!queue.enqueue(&overflow_tcb), "Enqueue when full must be rejected");

    queue.clear();
    TEST_ASSERT(queue.is_empty(), "Clear must empty queue");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 5: Stack Allocator Calculations, Intervals & Guard Page Math
// -----------------------------------------------------------------------------
void test_stack_allocator_math() {
    printf(" [RUN]  Stack Allocator Layout, Guard Pages & Isolation Math ...");

    // Layout configuration constants:
    // Slot size = (1 guard page + 4 stack pages) * 4096 = 20 KiB
    TEST_ASSERT(StackAllocator::STACK_USABLE_PAGES == 4, "Must be 4 usable pages");
    TEST_ASSERT(StackAllocator::GUARD_PAGES == 1, "Must be 1 guard page");
    TEST_ASSERT(StackAllocator::PAGES_PER_SLOT == 5, "Must be 5 pages per slot");
    TEST_ASSERT(StackAllocator::SLOT_SIZE_BYTES == 20480, "Slot size must be 20480 bytes");

    for (size_t slot = 0; slot < StackAllocator::MAX_STACK_SLOTS; ++slot) {
        uint64_t slot_vbase = StackAllocator::THREAD_STACK_BASE_VIRT + slot * StackAllocator::SLOT_SIZE_BYTES;
        uint64_t guard_va = slot_vbase;
        uint64_t bottom_va = slot_vbase + 4096;
        uint64_t top_va = slot_vbase + StackAllocator::SLOT_SIZE_BYTES;

        // Guard page verification
        TEST_ASSERT(guard_va % 4096 == 0, "Guard page must be 4 KiB aligned");
        TEST_ASSERT(bottom_va - guard_va == 4096, "Guard page must be exactly 4096 bytes");
        TEST_ASSERT(top_va - bottom_va == 16384, "Usable stack must be exactly 16 KiB");
        TEST_ASSERT(top_va % 16 == 0, "Stack top must be 16-byte aligned for SysV ABI");

        // Non-overlapping check with next slot
        if (slot + 1 < StackAllocator::MAX_STACK_SLOTS) {
            uint64_t next_slot_vbase = StackAllocator::THREAD_STACK_BASE_VIRT + (slot + 1) * StackAllocator::SLOT_SIZE_BYTES;
            TEST_ASSERT(top_va == next_slot_vbase, "Adjacent slots must abut cleanly without gaps or overlap");
        }
    }

    // Guard page address inquiry test
    for (size_t slot = 0; slot < 5; ++slot) {
        uint64_t slot_vbase = StackAllocator::THREAD_STACK_BASE_VIRT + slot * StackAllocator::SLOT_SIZE_BYTES;
        // Address inside guard page (offset 0 .. 4095)
        TEST_ASSERT(StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(slot_vbase)),
                    "Base of slot must be recognized as guard page");
        TEST_ASSERT(StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(slot_vbase + 2048)),
                    "Middle of guard page must be recognized as guard page");

        // Address inside usable stack (offset 4096 .. 20479)
        TEST_ASSERT(!StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(slot_vbase + 4096)),
                    "Usable stack bottom must NOT be recognized as guard page");
        TEST_ASSERT(!StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(slot_vbase + 8192)),
                    "Usable stack interior must NOT be recognized as guard page");
    }

    // Out of window addresses
    TEST_ASSERT(!StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(0)),
                "Null address is not a thread guard page");
    TEST_ASSERT(!StackAllocator::is_guard_page_address(llamaos::memory::VirtualAddress(0xFFFFFFFF80000000ULL)),
                "Low direct map is not a thread guard page");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 6: Thread Control Block Invariants & Metrics
// -----------------------------------------------------------------------------
void test_tcb_invariants() {
    printf(" [RUN]  Thread Control Block Invariants & Metrics            ...");

    ThreadControlBlock tcb{};
    tcb.id = 10;
    tcb.state = ThreadState::Created;
    tcb.timeslice_ticks = 2;
    tcb.ticks_consumed = 0;
    tcb.ticks_allocated = 0;

    // Simulate timer ticks accounting
    tcb.ticks_consumed++;
    tcb.ticks_allocated++;
    TEST_ASSERT(tcb.ticks_consumed == 1, "Consumed ticks must be 1");
    TEST_ASSERT(tcb.ticks_consumed < tcb.timeslice_ticks, "Timeslice should not be expired at 1 tick");

    tcb.ticks_consumed++;
    tcb.ticks_allocated++;
    TEST_ASSERT(tcb.ticks_consumed == 2, "Consumed ticks must be 2");
    TEST_ASSERT(tcb.ticks_consumed >= tcb.timeslice_ticks, "Timeslice MUST expire at 2 ticks");

    // Reset on context switch
    tcb.ticks_consumed = 0;
    tcb.switch_count++;
    tcb.preemption_count++;
    TEST_ASSERT(tcb.ticks_consumed == 0, "Consumed ticks reset on switch");
    TEST_ASSERT(tcb.switch_count == 1, "Switch count updated");
    TEST_ASSERT(tcb.preemption_count == 1, "Preemption count updated");
    TEST_ASSERT(tcb.ticks_allocated == 2, "Total allocated ticks preserved");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Test 7: Scheduler Invariants & Slot Recycling Simulation
// -----------------------------------------------------------------------------
void test_scheduler_invariants_and_recycling() {
    printf(" [RUN]  Scheduler Invariants & Slot Recycling Simulation      ...");

    // 1. Invariants verification on ReadyQueue and TCB states
    ReadyQueue rq{};
    ThreadControlBlock current_running{};
    current_running.id = 1;
    current_running.state = ThreadState::Running;

    ThreadControlBlock idle_tcb{};
    idle_tcb.id = 2;
    idle_tcb.state = ThreadState::Idle;
    idle_tcb.is_idle = true;

    // Invariant 1: Running thread cannot be enqueued in ready queue
    TEST_ASSERT(!rq.enqueue(&current_running), "Running thread must NOT be enqueued");

    // Invariant 2: Idle thread cannot be enqueued in ready queue
    TEST_ASSERT(!rq.enqueue(&idle_tcb), "Idle thread must NOT be enqueued");

    // Invariant 3: Blocked thread cannot be enqueued
    ThreadControlBlock blocked_tcb{};
    blocked_tcb.id = 3;
    blocked_tcb.state = ThreadState::Blocked;
    TEST_ASSERT(!rq.enqueue(&blocked_tcb), "Blocked thread must NOT be enqueued");

    // Invariant 4: Terminated thread cannot be enqueued
    ThreadControlBlock term_tcb{};
    term_tcb.id = 4;
    term_tcb.state = ThreadState::Terminated;
    TEST_ASSERT(!rq.enqueue(&term_tcb), "Terminated thread must NOT be enqueued");

    // Invariant 5: Duplicate prevention
    ThreadControlBlock worker1{};
    worker1.id = 5;
    worker1.state = ThreadState::Ready;
    TEST_ASSERT(rq.enqueue(&worker1), "Initial enqueue of worker1 must succeed");
    TEST_ASSERT(!rq.enqueue(&worker1), "Duplicate enqueue of worker1 must be rejected");
    TEST_ASSERT(rq.verify_invariants(), "ReadyQueue invariants must hold");

    // 2. Slot Recycling & Memory Leak Simulation across 3 batches
    bool slot_in_use[StackAllocator::MAX_STACK_SLOTS]{false};
    uint64_t total_alloc = 0;
    uint64_t total_freed = 0;

    // Reserve slot 0 for idle thread
    slot_in_use[0] = true;
    total_alloc += StackAllocator::STACK_USABLE_PAGES;

    for (int batch = 0; batch < 3; ++batch) {
        int allocated_slots[4]{-1, -1, -1, -1};
        // Allocate 4 slots
        for (int i = 0; i < 4; ++i) {
            for (size_t s = 1; s < StackAllocator::MAX_STACK_SLOTS; ++s) {
                if (!slot_in_use[s]) {
                    allocated_slots[i] = static_cast<int>(s);
                    slot_in_use[s] = true;
                    total_alloc += StackAllocator::STACK_USABLE_PAGES;
                    break;
                }
            }
            TEST_ASSERT(allocated_slots[i] >= 1, "Slot allocation must succeed");
        }

        // Free the 4 slots (simulate deferred reclamation)
        for (int i = 0; i < 4; ++i) {
            int s = allocated_slots[i];
            TEST_ASSERT(slot_in_use[s], "Slot must be in use before free");
            slot_in_use[s] = false;
            total_freed += StackAllocator::STACK_USABLE_PAGES;
        }
    }

    // After 3 batches of 4 threads (12 thread lifecycles), verify net allocation:
    // Only slot 0 (idle thread) remains active
    size_t active_slots = 0;
    for (size_t s = 0; s < StackAllocator::MAX_STACK_SLOTS; ++s) {
        if (slot_in_use[s]) active_slots++;
    }
    TEST_ASSERT(active_slots == 1, "Only idle slot must remain active");
    TEST_ASSERT(total_alloc - total_freed == StackAllocator::STACK_USABLE_PAGES,
                "Net allocated pages must equal exactly idle thread usable pages (no leak)");

    // Preemption timeslice constant verification
    TEST_ASSERT(DEFAULT_TIMESLICE_TICKS == 2, "Default timeslice must be 2 ticks (20 ms at 100 Hz)");

    printf(" PASSED\n");
}

// -----------------------------------------------------------------------------
// Main Runner
// -----------------------------------------------------------------------------
int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Phase 5 Process & Threading Host Unit Regression Suite\n");
    printf("================================================================================\n");

    test_thread_state_machine();
    test_context_layout();
    test_context_initialization();
    test_ready_queue();
    test_stack_allocator_math();
    test_tcb_invariants();
    test_scheduler_invariants_and_recycling();

    printf("================================================================================\n");
    printf(" Phase 5 Regression Suite Complete: ALL tests PASSED (%zu assertions verified)\n", g_assert_count);
    printf("================================================================================\n");

    return 0;
}
