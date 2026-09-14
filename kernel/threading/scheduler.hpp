#pragma once

#include "core/types.hpp"
#include "threading/thread.hpp"
#include "threading/ready_queue.hpp"
#include "threading/stack_allocator.hpp"

// =============================================================================
// LlamaOS/A - Kernel Thread Scheduler Foundation
// =============================================================================
// Provides round-robin preemptive kernel thread scheduling, thread lifecycle
// management (creation, voluntary yield, blocking, termination), safe stack
// isolation with guard pages, and deferred stack reclamation.
// =============================================================================

namespace llamaos::threading {

struct SchedulerStats {
    uint64_t total_threads{0};
    uint64_t active_threads{0};
    uint64_t terminated_threads{0};
    uint64_t stack_pages_allocated{0};
    uint64_t stack_pages_reclaimed{0};
    uint64_t context_switches{0};
    uint64_t timer_preemptions{0};
    uint64_t voluntary_yields{0};
};

class Scheduler {
public:
    static constexpr size_t MAX_THREADS = 64;

    // Initializes scheduler structures, registers bootstrap thread and idle thread
    static void init();

    // Activates scheduler preemptive loop
    static void start();

    // Creates and enqueues a new kernel thread with independent PMM/VMM stack
    static ThreadControlBlock* create_thread(const char* name,
                                            ThreadEntry entry,
                                            void* argument = nullptr,
                                            ThreadPriority priority = ThreadPriority::Normal);

    // Voluntary CPU yield by current running thread
    static void yield();

    // Central scheduling dispatcher
    static void schedule();

    // Explicit termination of current thread
    [[noreturn]] static void exit();

    // Suspends a thread (transitions to Blocked state)
    static void block(ThreadControlBlock* thread = nullptr);

    // Awakes a suspended thread (transitions from Blocked to Ready)
    static void unblock(ThreadControlBlock* thread);

    // Periodic timer interrupt tick hook for timeslice accounting and preemption
    static void on_timer_tick();

    // Inquiries
    static ThreadControlBlock* current_thread() noexcept;
    static ThreadControlBlock* idle_thread() noexcept;
    static ThreadControlBlock* bootstrap_thread() noexcept;
    static bool is_running() noexcept;
    static const SchedulerStats& stats() noexcept;

    // Diagnostic logging and architectural invariant verification
    static void dump_threads();
    static bool verify_invariants() noexcept;

private:
    static ThreadId allocate_id();
    static ThreadControlBlock* allocate_tcb();
    static void reclaim_deferred_stacks();

    static ThreadControlBlock s_tcb_pool[MAX_THREADS];
    static ReadyQueue s_ready_queue;

    static ThreadControlBlock* s_current_thread;
    static ThreadControlBlock* s_idle_thread;
    static ThreadControlBlock* s_bootstrap_thread;

    static SchedulerStats s_stats;
    static ThreadId s_next_id;
    static volatile bool s_running;
    static volatile bool s_in_scheduler;
    static volatile bool s_preemption_in_progress;
};

} // namespace llamaos::threading
