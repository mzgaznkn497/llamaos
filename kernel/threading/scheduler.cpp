#include "scheduler.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "arch/x86_64/cpu/interrupts.hpp"
#include "arch/x86_64/cpu/tss.hpp"
#include "arch/x86_64/cpu/timer.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Kernel Thread Scheduler Implementation
// =============================================================================

extern "C" void context_switch(llamaos::threading::ThreadContext* old_ctx,
                               const llamaos::threading::ThreadContext* new_ctx);
extern "C" void thread_bootstrap_trampoline();

extern "C" void thread_exit() {
    llamaos::threading::Scheduler::exit();
}

extern "C" void schedule_voluntary_yield() {
    llamaos::threading::Scheduler::yield();
}

extern "C" uint8_t kernel_stack_bottom[];
extern "C" uint8_t kernel_stack_top[];

namespace llamaos::threading {

ThreadControlBlock Scheduler::s_tcb_pool[MAX_THREADS]{};
ReadyQueue Scheduler::s_ready_queue{};

ThreadControlBlock* Scheduler::s_current_thread{nullptr};
ThreadControlBlock* Scheduler::s_idle_thread{nullptr};
ThreadControlBlock* Scheduler::s_bootstrap_thread{nullptr};

SchedulerStats Scheduler::s_stats{};
ThreadId Scheduler::s_next_id{1};
volatile bool Scheduler::s_running{false};
volatile bool Scheduler::s_in_scheduler{false};
volatile bool Scheduler::s_preemption_in_progress{false};

static void idle_worker(void*) {
    while (true) {
        arch::x86_64::halt();
    }
}

void Scheduler::init() {
    StackAllocator::init();
    s_ready_queue.clear();

    for (size_t i = 0; i < MAX_THREADS; ++i) {
        s_tcb_pool[i] = ThreadControlBlock{};
    }

    s_stats = SchedulerStats{};
    s_next_id = 1;
    s_running = false;
    s_in_scheduler = false;
    s_preemption_in_progress = false;

    // 1. Setup Bootstrap Thread (represented as current execution thread)
    ThreadControlBlock* b_tcb = &s_tcb_pool[0];
    b_tcb->id = BOOTSTRAP_THREAD_ID;
    llamaos::strncpy(b_tcb->name, "bootstrap", sizeof(b_tcb->name));
    b_tcb->state = ThreadState::Running;
    b_tcb->priority = ThreadPriority::Normal;
    b_tcb->is_bootstrap = true;
    b_tcb->active = true;
    b_tcb->stack.usable_bytes = 65536;
    b_tcb->stack.stack_bottom = memory::VirtualAddress(reinterpret_cast<uint64_t>(kernel_stack_bottom));
    b_tcb->stack.stack_top = memory::VirtualAddress(reinterpret_cast<uint64_t>(kernel_stack_top));

    s_current_thread = b_tcb;
    s_bootstrap_thread = b_tcb;
    s_stats.total_threads++;
    s_stats.active_threads++;

    // 2. Setup Dedicated System Idle Thread
    ThreadControlBlock* i_tcb = &s_tcb_pool[1];
    i_tcb->id = IDLE_THREAD_ID;
    llamaos::strncpy(i_tcb->name, "idle", sizeof(i_tcb->name));
    i_tcb->state = ThreadState::Idle;
    i_tcb->priority = ThreadPriority::Idle;
    i_tcb->is_idle = true;
    i_tcb->active = true;

    if (!StackAllocator::allocate_stack(&i_tcb->stack)) {
        klog_error("Scheduler Init: Failed to allocate stack for Idle thread!");
        return;
    }

    init_thread_context(i_tcb,
                        idle_worker,
                        nullptr,
                        reinterpret_cast<uintptr_t>(thread_bootstrap_trampoline));

    s_idle_thread = i_tcb;
    s_stats.total_threads++;
    s_stats.active_threads++;
    s_stats.stack_pages_allocated += StackAllocator::STACK_USABLE_PAGES;

    s_next_id = 3; // Next thread created will receive ID 3

    // 3. Register scheduler tick hook in PIT timer
    arch::x86_64::Timer::set_tick_hook(on_timer_tick);

    klog_info("Scheduler initialized: Bootstrap Thread ID=%u, Idle Thread ID=%u (Stack Top=%p)",
              b_tcb->id, i_tcb->id, i_tcb->stack.stack_top.as_ptr());
}

void Scheduler::start() {
    s_running = true;
    klog_info("Scheduler started: Preemptive multithreading active (PIT IRQ0 at 100 Hz).");
    arch::x86_64::enable_interrupts();
}

ThreadControlBlock* Scheduler::create_thread(const char* name,
                                            ThreadEntry entry,
                                            void* argument,
                                            ThreadPriority priority) {
    if (!entry) return nullptr;

    uint64_t saved = arch::x86_64::save_and_disable_interrupts();

    ThreadControlBlock* tcb = allocate_tcb();
    if (!tcb) {
        klog_error("Scheduler: Out of TCB slots (max %u threads)!", static_cast<uint32_t>(MAX_THREADS));
        arch::x86_64::restore_interrupt_state(saved);
        return nullptr;
    }

    if (!StackAllocator::allocate_stack(&tcb->stack)) {
        klog_error("Scheduler: Failed to allocate stack for new thread '%s'!", name ? name : "unnamed");
        tcb->active = false;
        arch::x86_64::restore_interrupt_state(saved);
        return nullptr;
    }

    tcb->id = allocate_id();
    if (name) {
        llamaos::strncpy(tcb->name, name, sizeof(tcb->name));
    } else {
        llamaos::strncpy(tcb->name, "thread", sizeof(tcb->name));
    }
    tcb->state = ThreadState::Ready;
    tcb->priority = priority;
    tcb->active = true;
    tcb->ticks_allocated = 0;
    tcb->ticks_consumed = 0;
    tcb->timeslice_ticks = DEFAULT_TIMESLICE_TICKS;
    tcb->switch_count = 0;
    tcb->preemption_count = 0;
    tcb->voluntary_yield_count = 0;

    init_thread_context(tcb,
                        entry,
                        argument,
                        reinterpret_cast<uintptr_t>(thread_bootstrap_trampoline));

    if (!s_ready_queue.enqueue(tcb)) {
        klog_error("Scheduler: Failed to enqueue thread ID=%u to ready queue!", tcb->id);
        StackAllocator::free_stack(tcb->stack);
        tcb->active = false;
        arch::x86_64::restore_interrupt_state(saved);
        return nullptr;
    }

    s_stats.total_threads++;
    s_stats.active_threads++;
    s_stats.stack_pages_allocated += StackAllocator::STACK_USABLE_PAGES;

    arch::x86_64::restore_interrupt_state(saved);
    return tcb;
}

void Scheduler::yield() {
    uint64_t saved = arch::x86_64::save_and_disable_interrupts();
    if (s_current_thread && !s_current_thread->is_idle) {
        s_current_thread->voluntary_yield_count++;
        s_stats.voluntary_yields++;
    }
    schedule();
    arch::x86_64::restore_interrupt_state(saved);
}

void Scheduler::schedule() {
    if (!s_running || s_in_scheduler) return;
    s_in_scheduler = true;

    // 1. Perform deferred reclamation of terminated thread stacks
    reclaim_deferred_stacks();

    ThreadControlBlock* cur = s_current_thread;
    ThreadControlBlock* next = s_ready_queue.dequeue();

    if (!next) {
        // No ready worker thread in queue
        if (cur && (cur->state == ThreadState::Running || cur->state == ThreadState::Ready)) {
            cur->state = ThreadState::Running;
            s_in_scheduler = false;
            return;
        }
        // Current thread blocked or terminated; select idle thread
        next = s_idle_thread;
    }

    if (cur == next) {
        cur->state = ThreadState::Running;
        s_in_scheduler = false;
        return;
    }

    // 2. Update state of yielding thread
    if (cur && cur->state == ThreadState::Running) {
        if (!cur->is_idle) {
            cur->state = ThreadState::Ready;
            (void)s_ready_queue.enqueue(cur);
        } else {
            cur->state = ThreadState::Idle;
        }
    }

    // 3. Update state of selected thread
    next->state = ThreadState::Running;
    s_current_thread = next;
    next->ticks_consumed = 0;
    next->switch_count++;
    s_stats.context_switches++;

    // 4. Keep TSS rsp0 updated with active kernel stack top
    if (arch::x86_64::TssManager::tss() && next->stack.stack_top.value() != 0) {
        arch::x86_64::TssManager::tss()->rsp0 = next->stack.stack_top.value();
    }

    ThreadControlBlock* old = cur;
    s_in_scheduler = false;

    if (s_preemption_in_progress && s_stats.timer_preemptions <= 6 && old && old != next) {
        klog_info("[PREEMPTION_SWITCH] Switch from '%s' (TID %u) -> '%s' (TID %u)",
                  old->name, old->id, next->name, next->id);
    }

    // Switch CR3 to target thread's address space (or kernel root)
    uint64_t target_cr3 = next->cr3 ? next->cr3 : memory::g_vmm.root_pml4_address().value();
    uint64_t cur_cr3 = arch::x86_64::read_cr3();
    if (target_cr3 != 0 && target_cr3 != cur_cr3) {
        memory::VirtualMemoryManager::reload_cr3(memory::PhysicalAddress(target_cr3));
    }

    // 5. Hardware context switch
    context_switch(&old->context, &next->context);
}

[[noreturn]] void Scheduler::exit() {
    arch::x86_64::disable_interrupts();

    ThreadControlBlock* cur = s_current_thread;
    if (cur) {
        cur->state = ThreadState::Terminated;
    }

    s_in_scheduler = true;

    // Reclaim any previously terminated threads (safe: executing on cur's stack, not theirs)
    reclaim_deferred_stacks();

    ThreadControlBlock* next = s_ready_queue.dequeue();
    if (!next) {
        next = s_idle_thread;
    }

    next->state = ThreadState::Running;
    s_current_thread = next;
    next->ticks_consumed = 0;
    next->switch_count++;
    s_stats.context_switches++;

    if (arch::x86_64::TssManager::tss() && next->stack.stack_top.value() != 0) {
        arch::x86_64::TssManager::tss()->rsp0 = next->stack.stack_top.value();
    }

    s_in_scheduler = false;

    // Switch CR3 to target thread's address space (or kernel root)
    uint64_t target_cr3 = next->cr3 ? next->cr3 : memory::g_vmm.root_pml4_address().value();
    uint64_t cur_cr3 = arch::x86_64::read_cr3();
    if (target_cr3 != 0 && target_cr3 != cur_cr3) {
        memory::VirtualMemoryManager::reload_cr3(memory::PhysicalAddress(target_cr3));
    }

    // Switch away from terminated thread context; will NEVER return to this stack!
    context_switch(&cur->context, &next->context);

    // Unreachable safety loop
    while (true) {
        arch::x86_64::halt();
    }
}

void Scheduler::block(ThreadControlBlock* thread) {
    uint64_t saved = arch::x86_64::save_and_disable_interrupts();
    ThreadControlBlock* target = thread ? thread : s_current_thread;
    if (!target) {
        arch::x86_64::restore_interrupt_state(saved);
        return;
    }

    if (target == s_current_thread) {
        target->state = ThreadState::Blocked;
        schedule();
    } else {
        s_ready_queue.remove(target);
        target->state = ThreadState::Blocked;
    }

    arch::x86_64::restore_interrupt_state(saved);
}

void Scheduler::unblock(ThreadControlBlock* thread) {
    if (!thread) return;
    uint64_t saved = arch::x86_64::save_and_disable_interrupts();

    if (thread->state == ThreadState::Blocked) {
        thread->state = ThreadState::Ready;
        (void)s_ready_queue.enqueue(thread);
    }

    arch::x86_64::restore_interrupt_state(saved);
}

void Scheduler::on_timer_tick() {
    if (!s_running || s_in_scheduler) return;

    ThreadControlBlock* cur = s_current_thread;
    if (!cur) return;

    cur->ticks_consumed++;
    cur->ticks_allocated++;

    // Check if timeslice expired or if current thread is idle and ready workers exist
    bool timeslice_expired = (cur->ticks_consumed >= cur->timeslice_ticks);
    bool idle_preemption = (cur->is_idle && !s_ready_queue.is_empty());

    if (timeslice_expired || idle_preemption) {
        cur->preemption_count++;
        s_stats.timer_preemptions++;
        if (s_stats.timer_preemptions <= 6) {
            klog_info("[PREEMPTION_EVENT #%llu] Tick %llu: Thread '%s' (TID %u) preempted by PIT IRQ0",
                      s_stats.timer_preemptions, arch::x86_64::Timer::ticks(), cur->name, cur->id);
        }
        s_preemption_in_progress = true;
        schedule();
        s_preemption_in_progress = false;
    }
}

ThreadControlBlock* Scheduler::current_thread() noexcept {
    return s_current_thread;
}

ThreadControlBlock* Scheduler::idle_thread() noexcept {
    return s_idle_thread;
}

ThreadControlBlock* Scheduler::bootstrap_thread() noexcept {
    return s_bootstrap_thread;
}

bool Scheduler::is_running() noexcept {
    return s_running;
}

const SchedulerStats& Scheduler::stats() noexcept {
    return s_stats;
}

ThreadId Scheduler::allocate_id() {
    ThreadId candidate = s_next_id++;
    if (s_next_id == 0) s_next_id = 3; // Overflow wrap protection
    return candidate;
}

ThreadControlBlock* Scheduler::allocate_tcb() {
    for (size_t i = 2; i < MAX_THREADS; ++i) {
        if (!s_tcb_pool[i].active) {
            s_tcb_pool[i] = ThreadControlBlock{};
            return &s_tcb_pool[i];
        }
    }
    return nullptr;
}

void Scheduler::reclaim_deferred_stacks() {
    for (size_t i = 0; i < MAX_THREADS; ++i) {
        ThreadControlBlock* t = &s_tcb_pool[i];
        if (t->active && t->state == ThreadState::Terminated && t != s_current_thread) {
            if (t->stack.slot_index >= 0) {
                StackAllocator::free_stack(t->stack);
                t->stack = ThreadStackInfo{};
                t->active = false;
                s_stats.stack_pages_reclaimed += StackAllocator::STACK_USABLE_PAGES;
                s_stats.terminated_threads++;
                if (s_stats.active_threads > 0) {
                    s_stats.active_threads--;
                }
            }
        }
    }
}

void Scheduler::dump_threads() {
    klog_info("--- Scheduler Thread Dump (Total=%u, Active=%u, Switches=%llu, Preemptions=%llu, Yields=%llu) ---",
              static_cast<uint32_t>(s_stats.total_threads),
              static_cast<uint32_t>(s_stats.active_threads),
              s_stats.context_switches,
              s_stats.timer_preemptions,
              s_stats.voluntary_yields);

    for (size_t i = 0; i < MAX_THREADS; ++i) {
        const auto& t = s_tcb_pool[i];
        if (t.active || t.state != ThreadState::Created) {
            klog_info("  [TCB %2u] ID=%u '%s' State=%s RSP=%p Top=%p Switches=%llu Preempts=%llu Yields=%llu",
                      static_cast<uint32_t>(i),
                      t.id,
                      t.name,
                      to_string(t.state),
                      reinterpret_cast<void*>(t.context.rsp),
                      t.stack.stack_top.as_ptr(),
                      t.switch_count,
                      t.preemption_count,
                      t.voluntary_yield_count);
        }
    }
}

bool Scheduler::verify_invariants() noexcept {
    // Invariant 1: Exactly one RUNNING thread on single-core CPU
    if (!s_current_thread || !s_current_thread->active) return false;
    if (s_current_thread->state != ThreadState::Running) return false;

    size_t running_count = 0;
    for (size_t i = 0; i < MAX_THREADS; ++i) {
        if (s_tcb_pool[i].active && s_tcb_pool[i].state == ThreadState::Running) {
            running_count++;
        }
    }
    if (running_count != 1) return false;

    // Invariant 2: RUNNING thread cannot be present in READY queue
    if (s_ready_queue.contains(s_current_thread)) return false;

    // Invariant 3: Dedicated system threads must exist, be active, and valid
    if (!s_idle_thread || !s_bootstrap_thread) return false;
    if (!s_idle_thread->active || !s_bootstrap_thread->active) return false;
    if (!s_idle_thread->is_idle || !s_bootstrap_thread->is_bootstrap) return false;
    if (s_idle_thread->state == ThreadState::Terminated) return false;

    // Invariant 4: Idle thread cannot be enqueued in regular ready queue
    if (s_ready_queue.contains(s_idle_thread)) return false;

    // Invariant 5: ReadyQueue internal invariants (FIFO, bounded, valid states, no duplicates)
    if (!s_ready_queue.verify_invariants()) return false;

    // Invariant 6: Terminated or Blocked threads are never present in ready queue;
    // all active Ready threads MUST be present in ready queue;
    // currently running thread cannot be in Terminated state.
    for (size_t i = 0; i < MAX_THREADS; ++i) {
        const auto* t = &s_tcb_pool[i];
        if (!t->active) continue;

        if (t->state == ThreadState::Terminated || t->state == ThreadState::Blocked) {
            if (s_ready_queue.contains(t)) return false;
        }

        if (t->state == ThreadState::Ready && !t->is_idle) {
            if (!s_ready_queue.contains(t)) return false;
        }

        if (t->state == ThreadState::Terminated && t == s_current_thread) {
            return false;
        }

        // Invariant 7: Stack slots must be valid, allocated in StackAllocator, and mutually disjoint across active threads.
        if (t->stack.slot_index >= 0) {
            if (t->stack.slot_index >= static_cast<int32_t>(StackAllocator::MAX_STACK_SLOTS)) {
                return false;
            }
            if (!StackAllocator::is_slot_in_use(t->stack.slot_index)) {
                return false;
            }
            for (size_t j = i + 1; j < MAX_THREADS; ++j) {
                if (s_tcb_pool[j].active && s_tcb_pool[j].stack.slot_index >= 0 &&
                    s_tcb_pool[j].stack.slot_index == t->stack.slot_index) {
                    return false; // Two threads sharing same stack slot!
                }
            }
        }
    }

    // Invariant 8: Active running thread stack never unmapped while running
    if (s_current_thread->stack.slot_index >= 0) {
        if (!StackAllocator::is_slot_in_use(s_current_thread->stack.slot_index)) {
            return false;
        }
    }

    return true;
}

} // namespace llamaos::threading
