# LlamaOS/A - Phase 5 Process & Threading Subsystem Final Verification Report

**Document Version:** 1.0.0  
**Phase Status:** COMPLETED, AUDITED, VERIFIED & FROZEN  
**Target Milestone:** Phase 5 — Process & Threading Subsystem  
**Architecture:** x86-64 (AMD64 Long Mode, Ring 0 Supervisor Kernel-Only)  
**Host Environment:** Linux (x86_64), GCC 13.3.0, NASM 2.16.01, GNU Make / CMake 3.28, QEMU 8.2.2  

---

## 1. Executive Summary

Phase 5 transforms LlamaOS/A from a single-threaded kernel execution flow into a fully functional, preemptive multitasking operating system kernel. Operating strictly in x86-64 Ring 0 (supervisor mode), LlamaOS/A now supports independent kernel threads with dedicated, PMM-backed and VMM-mapped stacks, callee-saved execution contexts, SysV AMD64 16-byte stack alignment invariants, hardware-enforced 4 KiB guard pages, a circular FIFO ready queue with duplicate prevention, an idle thread halt loop, a deferred stack reclamation mechanism for clean thread termination, and preemptive time-slice scheduling driven by 100 Hz PIT IRQ0 timer interrupts.

All architectural requirements and constraints have been verified:
- **Kernel-Only Scope:** 100% Ring 0 supervisor mode; no Ring 3 user mode transitions, no syscall/sysret instructions, no ELF binary loading, no filesystem, and no networking.
- **Hardware Integration:** PIT IRQ0 (Vector 32) timer tick dispatching hook integrated into the Dual 8259 PIC interrupt pipeline without regression to Phase 1–4 subsystems.
- **Verification Coverage:** 7 host unit test suites verifying 524 assertions (`tests/test_phase5.cpp`), 7 live QEMU automated test modes (`scripts/test_phase5.py`), and full system boot regression testing under both BIOS and OVMF UEFI firmware.
- **Freeze Status:** Phase 1, Phase 2, Phase 3, and Phase 4 invariants remain verified and intact. Phase 5 is hereby audited and FROZEN.

---

## 2. Design Principles & Ring 0 Kernel-Only Model

LlamaOS/A Phase 5 adheres to the following core design principles:

1. **Strict Supervisor Execution (Ring 0 Only):**
   Every kernel thread executes with `CS = 0x0008` (Kernel Code Segment, DPL 0) and `SS = 0x0010` (Kernel Data Segment, DPL 0). No Ring 3 segment selectors or privilege level transitions are introduced in this phase.
2. **Zero Dynamic Kernel Heap Dependency in Core Scheduling:**
   All thread management data structures—including the `ThreadControlBlock` table (`s_threads[MAX_THREADS]`), the circular ready queue (`ReadyQueue`), and the stack allocation bitmap—are statically allocated and bounded. Scheduling decisions require zero heap allocations and execute in deterministic $O(1)$ time.
3. **Hardware-Enforced Stack Isolation:**
   Each kernel thread operates on its own dedicated 16 KiB stack backed by physical memory allocated from the PMM and mapped via the VMM. Every stack is preceded by an unmapped 4 KiB guard page to trap stack overflows instantly via the Phase 3 Page Fault (`#PF`) handler.
4. **Deferred Reclamation for Execution Integrity:**
   A running thread cannot safely unmap or deallocate the stack it is currently executing on. When a thread terminates via `Scheduler::exit()`, its stack reclamation is deferred until the next thread is successfully switched in and executing on a separate stack.
5. **Reentrancy Protection & Non-Reentrant Scheduler Lock:**
   The scheduler enforces reentrancy protection via `s_in_scheduler` flag and interrupt disabling (`save_and_disable_interrupts()`), ensuring interrupt handlers (such as PIT IRQ0 or PS/2 IRQ1) do not corrupt scheduler invariants or trigger nested context switches.

---

## 3. Thread Model, State Machine & Thread Control Block (TCB)

### 3.1 Thread States
The thread lifecycle is governed by a strongly typed finite state machine:
```cpp
enum class ThreadState : uint8_t {
    Created,     // Initialized but not yet placed in ready queue
    Ready,       // Runnable and enqueued in ReadyQueue
    Running,     // Currently executing on the CPU core
    Blocked,     // Awaiting an event or resource; not in ReadyQueue
    Terminated,  // Finished execution; stack scheduled for deferred reclamation
    Idle         // Dedicated idle thread running CPU halt loop
};
```

Valid state transitions:
- `Created -> Ready`: Upon `Scheduler::start()` or `Scheduler::create_thread()` adding the thread to `ReadyQueue`.
- `Ready -> Running`: When dequeued by `Scheduler::schedule()`.
- `Running -> Ready`: Preempted by PIT IRQ0 or cooperatively calling `Scheduler::yield()`.
- `Running -> Blocked`: Calling `Scheduler::block()`.
- `Blocked -> Ready`: Resumed by external event via `Scheduler::unblock()`.
- `Running -> Terminated`: Explicitly calling `Scheduler::exit()` or returning from the thread entry point.
- `Idle <-> Running`: Idle thread scheduled when and only when `ReadyQueue` is completely empty.

### 3.2 Thread Control Block (TCB)
The TCB is defined in `kernel/threading/thread.hpp`:
```cpp
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
    uint64_t            timeslice_ticks{DEFAULT_TIMESLICE_TICKS}; // 2 ticks = 20 ms @ 100 Hz
    uint64_t            switch_count{0};
    uint64_t            preemption_count{0};
    uint64_t            voluntary_yield_count{0};

    bool                is_idle{false};
    bool                is_bootstrap{false};
    bool                active{false};       // Slot in use in TCB pool
};
```

### 3.3 Thread ID Assignments
- **Thread ID 1 (`BOOTSTRAP_THREAD_ID = 1`):** The initial bootstrap thread representing the original kernel execution flow that booted Phase 1–4. Its stack is the bootstrap kernel stack (`0xFFFFFFFF8013B000`).
- **Thread ID 2 (`IDLE_THREAD_ID = 2`):** The dedicated kernel idle thread (`"idle"`). Allocated with a dedicated 16 KiB stack + 4 KiB guard page; executes `while (true) { hlt; }`.
- **Thread ID $\ge$ 3:** Dynamically created worker and test kernel threads (e.g. `preempt_a` is TID 3, `preempt_b` is TID 4).

---

## 4. Assembly Context Switch & Bootstrap Trampoline

Low-level context switching is implemented in pure x86-64 assembly in `kernel/arch/x86_64/cpu/context_switch.asm`.

### 4.1 Callee-Saved Register Context
In the System V AMD64 ABI, functions must preserve `RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, and `RSP`. The `ThreadContext` struct (`kernel/threading/thread.hpp`) captures exactly these registers plus `RIP`, `RFLAGS`, and alignment padding:

| Offset | Field | C++ Type | Description |
|---|---|---|---|
| `0x00` | `r15` | `uint64_t` | Callee-saved general-purpose register |
| `0x08` | `r14` | `uint64_t` | Callee-saved general-purpose register |
| `0x10` | `r13` | `uint64_t` | Callee-saved general-purpose register (holds `argument` during bootstrap) |
| `0x18` | `r12` | `uint64_t` | Callee-saved general-purpose register (holds `entry` during bootstrap) |
| `0x20` | `rbp` | `uint64_t` | Callee-saved frame pointer |
| `0x28` | `rbx` | `uint64_t` | Callee-saved general-purpose register |
| `0x30` | `rflags` | `uint64_t` | CPU status and control flags (`pushfq` / `popfq`, IF bit preserved) |
| `0x38` | `rsp` | `uint64_t` | Stack pointer (points to return RIP or initial aligned stack top) |
| `0x40` | `rip` | `uint64_t` | Instruction pointer (saved as `.resume`, loaded into `RAX` for `jmp rax`) |
| `0x48` | `reserved` | `uint64_t` | 8-byte padding to enforce 16-byte alignment (`sizeof = 80`, `alignof = 16`) |

`static_assert` statements in `thread.hpp` and host unit tests verify that `sizeof(ThreadContext) == 80`, `alignof(ThreadContext) == 16`, and all field byte offsets match the assembly offsets identically.

### 4.2 Context Switch Routine (`context_switch`)
```nasm
global context_switch
; void context_switch(ThreadContext* old_ctx, const ThreadContext* new_ctx);
; SysV AMD64 ABI:
;   RDI = old_ctx (pointer to ThreadContext of yielding thread)
;   RSI = new_ctx (pointer to ThreadContext of thread to activate)
context_switch:
    ; 1. Save old thread context if old_ctx is not null
    test rdi, rdi
    jz .load_new

    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbp
    mov [rdi + 40], rbx

    pushfq
    pop rax
    mov [rdi + 48], rax         ; saved rflags

    mov [rdi + 56], rsp         ; saved rsp (points to return address of call)

    lea rax, [rel .resume]
    mov [rdi + 64], rax         ; saved rip -> .resume

.load_new:
    ; 2. Load new thread context from new_ctx (RSI)
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbp, [rsi + 32]
    mov rbx, [rsi + 40]

    mov rax, [rsi + 48]
    push rax
    popfq                       ; restore rflags

    mov rsp, [rsi + 56]         ; switch stack pointer
    mov rax, [rsi + 64]         ; target rip

    jmp rax                     ; transfer control to new thread context

.resume:
    ; Resumed here when old_ctx is rescheduled
    ret
```

### 4.3 Bootstrap Trampoline (`thread_bootstrap_trampoline`)
When a newly created thread is switched to for the first time, `context_switch` loads `new_ctx->rip` (which points to `thread_bootstrap_trampoline`) and executes `jmp rax`, transferring execution directly to `thread_bootstrap_trampoline`:
```nasm
global thread_bootstrap_trampoline
extern thread_exit

thread_bootstrap_trampoline:
    ; System V ABI: direction flag must be clear
    cld

    ; Ensure interrupts are enabled for kernel thread execution
    sti

    ; Place argument in RDI (SysV ABI first argument register)
    mov rdi, r13
    call r12                    ; call entry(argument)

    ; If entry function returns, cleanly terminate thread
    call thread_exit

    ; Safety catch in case thread_exit returns (it must never return)
.hang:
    cli
    hlt
    jmp .hang
```

---

## 5. System V AMD64 ABI 16-byte Stack Alignment

The System V AMD64 ABI mandates that the stack pointer (`RSP`) must be 16-byte aligned (`RSP % 16 == 0`) immediately before a `call` instruction is executed. Upon function entry, because `call` pushes the 8-byte return address, the stack pointer satisfies:
$$\text{RSP} \pmod{16} == 8 \iff (\text{RSP} + 8) \pmod{16} == 0$$

LlamaOS/A enforces this invariant through two coordinated mechanisms:
1. **Initial Stack Pointer Setup:** In `init_thread_context()`, the initial stack pointer is calculated from the top of the 16 KiB usable stack and aligned down:
   ```cpp
   uint64_t top = stack.usable_top.value();
   top &= ~0xFULL; // Enforce 16-byte alignment: top % 16 == 0
   // Reserve 8 bytes of shadow/alignment space so top % 16 == 0 upon trampoline entry
   ctx->rsp = top;
   ctx->rip = reinterpret_cast<uint64_t>(thread_bootstrap_trampoline);
   ```
2. **Trampoline Call Alignment:** Inside `thread_bootstrap_trampoline`, `RSP % 16 == 0`. The subsequent `call r12` pushes an 8-byte address, placing the thread entry point in exact compliance with GCC-generated SSE/AVX vector code that expects `(RSP + 8) % 16 == 0`.

This alignment contract is verified both at compile time, in host unit tests (`test_initial_context_setup`), and during live QEMU execution (`test-context`).

---

## 6. Callee-Saved Register Preservation Verification

To verify empirically that `context_switch` does not corrupt or leak CPU register state across voluntary thread execution boundaries, a specialized verification routine was developed:
1. In `test_context_switch_reg_preservation()`, the caller sets all callee-saved registers to unique, non-trivial, 64-bit sentinel values:
   - `RBX = 0x1122334455667788`
   - `RBP = 0x2233445566778899`
   - `R12 = 0x33445566778899AA`
   - `R13 = 0x445566778899AABB`
   - `R14 = 0x5566778899AABBCC`
   - `R15 = 0x66778899AABBCCDD`
2. An auxiliary assembly routine (`test_reg_preservation_asm`) clobbers all registers with `0xDEADBEEF` while switching into a secondary worker thread.
3. The worker thread yields control back to the original caller via `Scheduler::yield()`.
4. Upon return, every register is compared against its original sentinel. In both host test simulations and live QEMU runs (`test-context`), all sentinel values are recovered intact across voluntary context switches (via `schedule_voluntary_yield()`). This test does not exercise preemptive register preservation directly, though preemption uses the same `context_switch()` save/restore path.

---

## 7. Stack Allocator, Guard Pages & Memory Isolation

### 7.1 Virtual Memory Mapping Layout
Thread stacks are managed by `StackAllocator` (`kernel/threading/stack_allocator.hpp`, `stack_allocator.cpp`). Stacks reside in a dedicated dynamic 4 KiB window inside `PDPT[509]`:
- **Window Base Virtual Address:** `0xFFFFFFFF72000000ULL`
- **Slot Architecture:** Up to 64 concurrent thread slots (`MAX_STACK_SLOTS = 64`).
- **Pages per Slot:** 5 pages (20 KiB total):
  - Page 0 (4 KiB): **Guard Page** (Non-present, unmapped).
  - Pages 1..4 (16 KiB): **Usable Stack** (Mapped `Present=1, Writable=1, NX=1, Supervisor=0`).

```
Virtual Address Space Layout for Thread Stacks (Slot N):
+-----------------------------------------------------------------------+
| Slot Base (VA): THREAD_STACK_BASE_VIRT + N * 20 KiB                   |
+------------------------------------+----------------------------------+
| 0x0000 - 0x0FFF (4 KiB)            | GUARD PAGE (Unmapped, P=0)       |
| 0x1000 - 0x1FFF (4 KiB)            | Usable Stack Frame 1 (P=1, W=1,NX=1)|
| 0x2000 - 0x2FFF (4 KiB)            | Usable Stack Frame 2 (P=1, W=1,NX=1)|
| 0x3000 - 0x3FFF (4 KiB)            | Usable Stack Frame 3 (P=1, W=1,NX=1)|
| 0x4000 - 0x4FFF (4 KiB)            | Usable Stack Frame 4 (P=1, W=1,NX=1)|
+------------------------------------+----------------------------------+
| Usable Top (Initial RSP): THREAD_STACK_BASE_VIRT + N * 20 KiB + 20 KiB |
+-----------------------------------------------------------------------+
```

### 7.2 Page Table Hierarchy Validation
The base address `0xFFFFFFFF72000000ULL` was selected deliberately to avoid colliding with:
1. `PDPT[510]`, which maps the kernel 2 MiB direct-map large pages (`0xFFFFFFFF80200000` to `0xFFFFFFFF90000000`).
2. `PDPT[510] PD1[256..511]`, which maps the higher-half MMIO / Framebuffer window (`0xFFFFFFFFA0000000` to `0xFFFFFFFFC0000000`).
3. `PDPT[509] PD[384]`, which maps the Phase 3 IST Stacks and TSS page (`0xFFFFFFFF70000000` to `0xFFFFFFFF70011000`).

`0xFFFFFFFF72000000ULL` maps to `PDPT[509] PD[400] PT[0]`. When `g_vmm.map_page()` is called, intermediate page directories and page tables are dynamically allocated as standard 4 KiB tables from the PMM, completely eliminating `HugePageCollision` errors.

### 7.3 Guard Page Overflow Detection
Stack growth on x86-64 proceeds downward (decreasing virtual addresses). If a thread's call frame exceeds the 16 KiB usable boundary, the next memory access touches the unmapped 4 KiB guard page at `slot_base`. This triggers an immediate hardware Page Fault (`#PF`, Vector 14), which is routed to the dedicated `IST2` stack, halting the misbehaving thread before adjacent thread stacks can be corrupted.

---

## 8. Ready Queue & Duplicate Prevention Invariants

The scheduler maintains runnable threads in a circular FIFO queue (`ReadyQueue`, `kernel/threading/ready_queue.hpp`):
- **Capacity:** Fixed capacity of 64 thread pointers (`CAPACITY = 64`).
- **O(1) Enqueue/Dequeue:** Managed via `m_head`, `m_tail`, and `m_count` circular indices.
- **Duplicate Prevention:** Before enqueueing a thread, `ReadyQueue::push()` scans existing entries. If the thread pointer is already present, the push is rejected and returns `false`.
- **State Invariant Enforcement:** Threads inserted into the queue must be in `ThreadState::Ready` or `ThreadState::Created`.
- **Compaction Routine:** `remove()` shifts remaining entries linearly to preserve strict FIFO ordering when a thread is dequeued or terminated prematurely.

---

## 9. Round-Robin Scheduler & Cooperative Context Switching

The scheduler (`kernel/threading/scheduler.hpp`, `scheduler.cpp`) implements a round-robin policy:
- **`Scheduler::init()`:** Creates the bootstrap TCB (`tid = 1`) for the current execution flow and initializes the idle thread (`tid = 2`).
- **`Scheduler::start()`:** Enqueues worker threads, transitions the current flow to `ThreadState::Running`, and enables preemptive scheduling.
- **`Scheduler::yield()`:** Cooperatively releases the CPU:
  1. Saves current CPU interrupt flag and disables interrupts (`save_and_disable_interrupts()`).
  2. Increments `yield_count` metric on current thread.
  3. Enqueues current thread back to `ReadyQueue` with state `ThreadState::Ready`.
  4. Calls `schedule()`.
  5. Restores original interrupt flag upon resumption.
- **`Scheduler::schedule()`:**
  1. Checks for deferred terminated stacks needing reclamation (`reclaim_deferred_stacks()`).
  2. Dequeues next runnable thread from `ReadyQueue`.
  3. If `ReadyQueue` is empty, selects the dedicated idle thread (`tid = 2`).
  4. Updates states: outgoing becomes `Ready`, incoming becomes `Running`.
  5. Executes low-level `context_switch(&old_ctx, &new_ctx)`.

### Live Verification: Cooperative Switching
In live QEMU test `test-scheduler`, two threads (`"worker_a"` and `"worker_b"`) cooperatively yield across 3 iterations. Serial logs confirm strict alternating execution:
```
[SCHED] Worker A - Iteration 1
[SCHED] Worker B - Iteration 1
[SCHED] Worker A - Iteration 2
[SCHED] Worker B - Iteration 2
[SCHED] Worker A - Iteration 3
[SCHED] Worker B - Iteration 3
[PASS] Cooperative Context Switching Verified (Interleaving Confirmed)
```

---

## 10. Preemptive Multitasking via PIT IRQ0 & Interrupt Return Path

Preemption allows the kernel to interrupt compute-bound threads without cooperative yielding:
1. **Timer Tick Hook:** `Timer::set_tick_hook(Scheduler::on_timer_tick)` registers the scheduler hook in `kernel/arch/x86_64/cpu/timer.cpp`.
2. **Interrupt Service Routine (ISR):**
   When PIT IRQ0 fires at 100 Hz:
   - CPU pushes `SS`, `RSP`, `RFLAGS`, `CS`, `RIP` to the current thread's active kernel stack (`IST = 0`).
   - `isr_stub_32` pushes dummy error code `0` and vector `32`.
   - `common_isr_stub` pushes all 15 GPRs forming the 176-byte `InterruptFrame`.
   - `exception_dispatch()` routes vector 32 to `handle_timer_interrupt()`.
   - `Timer::dispatch_tick()` sends Master PIC EOI (`PicManager::send_eoi(0)`), increments global tick counter, and invokes `Scheduler::on_timer_tick()`.
3. **Time-Slice Accounting:**
   `Scheduler::on_timer_tick()` increments `cur->ticks_consumed++` and `cur->ticks_allocated++`.
   Preemption triggers if:
   - `(cur->ticks_consumed >= cur->timeslice_ticks)` with `DEFAULT_TIMESLICE_TICKS = 2` (2 ticks = 20 ms @ 100 Hz), OR
   - `(cur->is_idle && !s_ready_queue.is_empty())` (immediate preemption of idle thread when work arrives).
   Upon preemption:
   - Increments `cur->preemption_count++` and `s_stats.timer_preemptions++`.
   - Invokes `Scheduler::schedule()`.
4. **Transparent Return:**
   `context_switch(&old->context, &next->context)` saves `old->context.rip` as `.resume`, which executes `ret` back into `schedule()`. When the preempted thread is later rescheduled, control returns up through `schedule() -> on_timer_tick() -> Timer::dispatch_tick() -> handle_timer_interrupt() -> exception_dispatch() -> common_isr_stub`. In `common_isr_stub`, all 15 GPRs are restored, vector/error code are removed (`add rsp, 16`), and `iretq` restores the hardware interrupt frame (`RIP`, `CS`, `RFLAGS`, `RSP`, `SS`), returning directly to the interrupted instruction with `RFLAGS.IF = 1`.

### Live Verification: Preemptive Multitasking
In live QEMU test `test-preemption`, two worker threads (`preempt_a` [TID 3] and `preempt_b` [TID 4]) execute tight compute loops without ever calling `yield()`. Serial logs verify both threads make concurrent progress and log preemption events triggered by timer ticks:
```
[PREEMPTION_EVIDENCE] Worker A counter=7884777, Worker B counter=10656004, Timer Preemptions=8, Switches=12
[PREEMPTION_TEST_PASS] Preemptive multitasking verified via PIT IRQ0.
```
- `Timer Preemptions` counts the number of `on_timer_tick()` invocations where timeslice expiration triggered `schedule()`. `Switches` counts total `context_switch()` calls (including voluntary yields by the bootstrap thread). The two counters measure different events and are not expected to be equal.
- Counter values vary between QEMU runs due to emulator scheduling latency.

---

## 11. Idle Thread Design & Halt Loop

The idle thread (`tid = 2`, `"idle"`) is designed to run when no other runnable threads exist:
- **Dedicated Stack:** Allocated an independent 16 KiB stack + 4 KiB guard page at `0xFFFFFFFF72000000ULL` to `0xFFFFFFFF72005000ULL`.
- **Halt Loop:**
  ```cpp
  void Scheduler::idle_thread_entry(void*) {
      while (true) {
          arch::x86_64::halt(); // Executes "hlt" instruction; wakes on interrupt
      }
  }
  ```
- **Scheduling Discipline:** The idle thread is **never** enqueued in `ReadyQueue`. It is scheduled solely as a fallback by `Scheduler::schedule()` when `ReadyQueue::pop()` returns `nullptr`.

---

## 12. Thread Lifecycle: Termination & Deferred Reclamation

When a thread completes execution:
1. It calls `Scheduler::exit()` (or returns from its entry function via `thread_exit()`).
2. The scheduler transitions the thread to `ThreadState::Terminated`.
3. The thread cannot free its own stack while executing on it; doing so would destroy its active execution context and corrupt memory.
4. Instead, `Scheduler::exit()` sets `state = Terminated`, invokes `reclaim_deferred_stacks()` to reclaim previously terminated threads, and switches immediately to the next ready thread via `context_switch()`.
5. Inside `reclaim_deferred_stacks()` (called during every `schedule()` and `exit()`):
   - The TCB pool (`s_tcb_pool[MAX_THREADS]`) is scanned for any active thread in `ThreadState::Terminated` where `t != s_current_thread`.
   - It unmaps the 4 usable stack pages via `g_vmm.unmap_page()`.
   - It frees the 4 physical page frames back to the PMM via `g_pmm.free_pages()`.
   - It marks the stack slot as available in `StackAllocator`.
   - It resets `t->stack` metadata and marks `t->active = false`.

### Live Verification: Deferred Reclamation & Rapid Batch Recycling
In live QEMU test `test-thread-exit`, a short worker thread spawns, executes, and terminates. Following this, 3 consecutive batches of 3 threads each (9 total threads) are rapidly created, executed, terminated, and deferred-reclaimed. Memory leak audits confirm that only the idle thread stack remains allocated:
```
[INFO] worker-start
[INFO] worker-exit
[INFO] scheduler-continues
[INFO] idle-running
[INFO] [RAPID_RECYCLE_PASS] 3 batches (9 threads) recycled without leaks. Slots in use=1, Reclaimed pages=40
[INFO] [THREAD_EXIT_PASS] Thread exit and deferred cleanup verified.
```

---

## 13. Concurrency Stress Test: 8 Concurrent Threads

To verify scheduling fairness, ready queue stability, and stack isolation under high thread density, `test-thread-stress` spawns 8 concurrent worker threads (`stress_0` .. `stress_7`).
- Each thread performs 100 iterations of compute work and yields every 10 iterations.
- A total of 800 thread iterations complete successfully. The scheduler records $\ge$50 context switches (source threshold: `stats.context_switches >= 50`; observed values range from 90–100 depending on QEMU scheduling latency).
- All 8 threads exit cleanly, and all 8 stacks are safely reclaimed without page table corruption or frame leakage.
- Representative output verified in live QEMU:
```
[INFO] Stress test completed: 8/8 threads finished, Context switches=99
[INFO] [STRESS_TEST_PASS] 8-thread concurrent workload completed successfully.
```

---

## 14. Integration with Phase 1–4 Subsystems

Phase 5 maintains complete backward compatibility and introduces zero regressions across previous milestones:
- **Phase 1 (Boot & Paging):** Multiboot2 parsing, higher-half direct mapping, and VGA/Serial consoles remain identical.
- **Phase 2 (PMM & VMM):** All 13 PMM gates and 14 VMM gates pass identically. Stack allocator pages are strictly allocated via `g_pmm.alloc_page()` and mapped via `g_vmm.map_page()`.
- **Phase 3 (Descriptors & Interrupts):** GDT, TSS, IST stacks, and CPU exception handlers (`#DF`, `#PF`, `#GP`) remain untouched and active. Timer interrupt IRQ0 routes ticks to the scheduler.
- **Phase 4 (Devices & Hardware):** PS/2 8042 controller, keyboard driver (IRQ1), PCI bus scan, and Linear Framebuffer (1024x768@32bpp) operate simultaneously with the scheduler. The interactive keyboard echo loop was updated to yield CPU time (`Scheduler::yield()`), demonstrating seamless device-scheduler cooperation.

---

## 15. Memory Layout and Page Table Hierarchy

The following table documents the virtual memory address space map following Phase 5 integration:

| Virtual Memory Region | Size | Page Table Mapping | Access / Protection | Purpose |
|---|---|---|---|---|
| `0x0000000000000000 - 0x00007FFFFFFFFFFF` | 128 TiB | `PML4[0..255]` | Unmapped (`P=0`) | User-space address window (preserved for Phase 6+) |
| `0xFFFFFFFF70000000 - 0xFFFFFFFF70001000` | 4 KiB | `PDPT[509] PD[384] PT[0]` | Unmapped (`P=0`) | IST Guard Page 1 |
| `0xFFFFFFFF70001000 - 0xFFFFFFFF70005000` | 16 KiB | `PDPT[509] PD[384] PT[1..4]` | `RW NX` Supervisor | IST1 (#DF Stack) |
| `0xFFFFFFFF70005000 - 0xFFFFFFFF70006000` | 4 KiB | `PDPT[509] PD[384] PT[5]` | Unmapped (`P=0`) | IST Guard Page 2 |
| `0xFFFFFFFF70006000 - 0xFFFFFFFF7000A000` | 16 KiB | `PDPT[509] PD[384] PT[6..9]` | `RW NX` Supervisor | IST2 (#PF Stack) |
| `0xFFFFFFFF7000A000 - 0xFFFFFFFF7000B000` | 4 KiB | `PDPT[509] PD[384] PT[10]` | Unmapped (`P=0`) | IST Guard Page 3 |
| `0xFFFFFFFF7000B000 - 0xFFFFFFFF7000F000` | 16 KiB | `PDPT[509] PD[384] PT[11..14]`| `RW NX` Supervisor | IST3 (#MC / Critical Stack) |
| `0xFFFFFFFF7000F000 - 0xFFFFFFFF70010000` | 4 KiB | `PDPT[509] PD[384] PT[15]` | Unmapped (`P=0`) | IST Guard Page 4 |
| `0xFFFFFFFF70010000 - 0xFFFFFFFF70011000` | 4 KiB | `PDPT[509] PD[384] PT[16]` | `RW NX` Supervisor | Task State Segment (TSS) Page |
| `0xFFFFFFFF72000000 - 0xFFFFFFFF72140000` | 1280 KiB| `PDPT[509] PD[400] PT[0..319]`| Dynamic 4 KiB (`RW NX`)| **Phase 5 Kernel Thread Stacks (64 slots x 20 KiB)** |
| `0xFFFFFFFF80000000 - 0xFFFFFFFF801FFFFF` | 2 MiB | `PDPT[510] PD1[0] PT0[0..511]`| Section W^X (4 KiB) | Kernel Image (.text, .rodata, .data, .bss, stack) |
| `0xFFFFFFFF80200000 - 0xFFFFFFFF9FFFFFFF` | 510 MiB| `PDPT[510] PD1[1..255]` | 2 MiB Huge Pages (`RW NX`) | Direct Physical Memory Map |
| `0xFFFFFFFFA0000000 - 0xFFFFFFFFBFFFFFFF` | 512 MiB| `PDPT[510] PD1[256..511]` | Dynamic 4 KiB (`RW NX`)| Phase 4 MMIO & Framebuffer Window |
| `0xFFFFFFFFC0000000 - 0xFFFFFFFFFFFFFFFF` | 1 GiB | `PDPT[511] PD2[0..511]` | 2 MiB Huge Pages (`RW NX`) | Extended Physical Direct Map |

---

## 16. Host Unit Test Suite Coverage & Assertion Breakdown

The host unit test suite (`tests/test_phase5.cpp`) executes freestanding test simulations without QEMU:

| Test Case | Description | Verified Assertions | Status |
|---|---|:---:|:---:|
| `test_thread_state_machine` | Validates all state transitions, `to_string()` representations, and priority levels | 16 | **PASS** |
| `test_context_layout` | Verifies `ThreadContext` size (80 bytes), 16-byte alignment, and exact field offsets (0x00 to 0x48) | 12 | **PASS** |
| `test_context_initialization`| Validates trampoline setup, function pointers, 16-byte initial RSP alignment, and RFLAGS IF bit | 9 | **PASS** |
| `test_ready_queue` | Exhaustive circular FIFO tests, capacity bounds (64), duplicate prevention, and state validation | 100 | **PASS** |
| `test_stack_allocator_math` | Validates slot arithmetic, guard page address detection, usable stack top calculation, and slot bounds across 64 slots | 345 | **PASS** |
| `test_tcb_invariants` | Verifies timeslice accounting, execution metrics, preemption counters, and switch counters | 8 | **PASS** |
| `test_scheduler_invariants_and_recycling` | Simulates multi-batch thread life cycles, invariant checks, and zero-leak slot recycling | 34 | **PASS** |
| **Total Phase 5 Unit Assertions** | | **524** | **PASS** |

Combined with Phase 1–4 host unit tests:
- Multiboot2 Parser Tests: 27/27 passed
- PMM Unit Tests: 17/17 passed (369 assertions)
- VMM Unit Tests: 10/10 passed (611 assertions)
- Descriptor & IDT Tests: 64 assertions passed
- Phase 4 Device Tests: 8,810 assertions passed
- Phase 5 Threading Tests: 524 assertions passed
- **Grand Total Host Verification:** **10,378 assertions across 5 assertion suites** plus **27 parser test cases** = **10,405 total host checks across all 6 targets (100% PASS)**.

---

## 17. Live QEMU Automated Verification Harness & Test Results

Automated live execution testing is implemented in `scripts/test_phase5.py`. Each test mode runs an isolated QEMU instance with ISA debug port `0xF4` exit handling:

| Test Mode | Boot Argument | Verification Tokens | Exit Code | Result |
|---|---|---|:---:|:---:|
| Cooperative Switching | `test-scheduler` | `[SCHED] Worker A - Iteration 1..3`, `[SCHED] Worker B - Iteration 1..3`, `[TEST_SCHEDULER_PASS]` | `33` | **PASS** |
| Preemptive Multitasking| `test-preemption`| `[PREEMPTION_EVENT #1..6]`, `[PREEMPTION_SWITCH]`, `[PREEMPTION_EVIDENCE]`, `[PREEMPTION_TEST_PASS]` | `33` | **PASS** |
| Callee-Saved Registers | `test-context` | `[REGISTER_PRESERVATION_PASS]` (sentinels preserved across voluntary context switches) | `33` | **PASS** |
| Stack Isolation & Guard| `test-stack` | `[STACK_ISOLATION_PASS]` (guard pages unmapped `P=0`, usable stacks mapped `RW NX`) | `33` | **PASS** |
| Thread Exit & Reclaim | `test-thread-exit` | `[RAPID_RECYCLE_PASS]`, `[THREAD_EXIT_PASS]` (40 pages reclaimed, 1 slot in use) | `33` | **PASS** |
| Concurrency Stress | `test-thread-stress` | `[STRESS_TEST_PASS]` (8 threads, 800 total iterations, $\ge$50 context switches) | `33` | **PASS** |
| Comprehensive Live | `test-phase5-live` | `[PHASE5_LIVE_PASS]` (TCB, stack allocator, ready queue validation) | `33` | **PASS** |

### Live Execution Summary
```
======================================================================
 Phase 5 Live Test Results: 7/7 PASSED
======================================================================
```

---

## 18. Failure Modes, Edge Cases & Robustness Analysis

1. **Stack Overflow Trap:**
   - *Risk:* A kernel thread exhausts its 16 KiB usable stack due to deep recursion or large buffers.
   - *Mitigation:* The adjacent 4 KiB guard page is unmapped. Any write or push below `stack_bottom` triggers an immediate `#PF` on vector 14. The `#PF` handler runs on dedicated `IST2`, capturing CR2 and halting the machine safely.
2. **Double Enqueue into Ready Queue:**
   - *Risk:* A thread is mistakenly added to the ready queue multiple times, corrupting circular pointers.
   - *Mitigation:* `ReadyQueue::enqueue()` scans existing elements before insertion, rejecting duplicates with duplicate prevention.
3. **Reentrancy During Scheduling:**
   - *Risk:* A hardware timer interrupt fires while the scheduler is executing `Scheduler::schedule()`, corrupting TCB pointers.
   - *Mitigation:* `save_and_disable_interrupts()` guards all scheduler state transitions, and `s_in_scheduler` prevents reentrant calls.
4. **Premature Stack Freeing on Termination:**
   - *Risk:* A thread frees its own stack while executing `Scheduler::exit()`.
   - *Mitigation:* `Scheduler::exit()` marks the thread as `Terminated` and switches contexts immediately via `context_switch()`. The incoming thread performs deferred stack deallocation via pool scan (`reclaim_deferred_stacks()`) while executing on a separate, valid stack.
5. **Huge Page Table Collision:**
   - *Risk:* Dynamically mapping a 4 KiB thread stack into an area previously mapped with 2 MiB large pages fails with `HugePageCollision`.
   - *Mitigation:* Thread stacks are anchored at `0xFFFFFFFF72000000ULL` (`PDPT[509] PD[400]`), where only 4 KiB page tables exist.

---

## 19. Exact Changes Introduced in Phase 5

The following files were created or modified for Phase 5:
- `kernel/threading/thread_types.hpp` *(NEW)*: Strongly typed `ThreadState`, `ThreadPriority`, `ThreadId`, and string formatters.
- `kernel/threading/thread.hpp` *(NEW)*: `ThreadContext` (80 bytes, 16-byte aligned), `ThreadStackInfo`, `ThreadControlBlock`, `init_thread_context()`.
- `kernel/threading/thread.cpp` *(NEW)*: TCB initialization, context setup, and helper routines.
- `kernel/threading/stack_allocator.hpp` *(NEW)*: 64-slot stack allocator with 16 KiB usable + 4 KiB guard page math.
- `kernel/threading/stack_allocator.cpp` *(NEW)*: Physical frame allocation via PMM, virtual mapping via VMM, and slot tracking.
- `kernel/threading/ready_queue.hpp` *(NEW)*: Bounded 64-element circular FIFO queue with duplicate prevention and compaction.
- `kernel/threading/scheduler.hpp` *(NEW)*: Scheduler interface (`init`, `start`, `schedule`, `yield`, `exit`, `block`, `unblock`, `create_thread`, `on_timer_tick`).
- `kernel/threading/scheduler.cpp` *(NEW)*: Scheduler implementation, idle thread, deferred reclamation, and reentrancy guards.
- `kernel/arch/x86_64/cpu/context_switch.asm` *(NEW)*: Callee-saved assembly `context_switch`, `thread_bootstrap_trampoline`, and `test_reg_preservation_asm`.
- `kernel/arch/x86_64/cpu/timer.hpp` & `timer.cpp` *(MODIFIED)*: Added `set_tick_hook()`, `dispatch_tick()`, and invocation in `handle_timer_interrupt()`.
- `kernel/kernel_main.cpp` *(MODIFIED)*: Initialized Phase 5 subsystem, added Milestone 5 verification banner, registered all 7 live test modes, and updated interactive loop to yield CPU time.
- `tests/test_phase5.cpp` *(NEW)*: 7 host unit test suites (524 assertions).
- `scripts/test_phase5.py` *(NEW)*: Automated QEMU test harness for Phase 5 live tests.
- `Makefile` & `CMakeLists.txt` *(MODIFIED)*: Added Phase 5 source files, host test binaries, and make/ctest targets.

---

## 20. Final Preemption & Context-Switch Audit

### 20.1 Actual IRQ0 Execution Path
The complete execution path from hardware timer trigger to thread preemption and resumption proceeds as follows:
1. **Hardware Timer Trigger:** Programmable Interval Timer (PIT 8254) Channel 0 operates in Mode 2 (Rate Generator, command byte `0x34`) at 100 Hz (divisor 11,932). When the counter reaches zero, the PIT raises the IRQ0 line connected to the Dual 8259A Master PIC.
2. **Interrupt Delivery to CPU:** The Master PIC routes IRQ0 to interrupt vector 32 (`0x20`). Because `s_idt[32]` is configured with `GateType::InterruptGate` and `IST = 0`, the x86-64 CPU automatically clears `RFLAGS.IF` (disabling interrupts) and pushes the 5 architectural qwords onto the *current thread's active kernel stack*:
   - `SS` (at `RSP - 8`)
   - `RSP` (at `RSP - 16`)
   - `RFLAGS` (at `RSP - 24`, capturing the thread's original IF=1 state)
   - `CS` (at `RSP - 32`, `0x0008`)
   - `RIP` (at `RSP - 40`, pointing to the next instruction in the interrupted thread)
3. **Assembly Vector Stub:** The CPU jumps to `isr_stub_32` in `interrupt_stubs.asm`:
   - `push qword 0` (pushes a dummy error code to normalize the frame)
   - `push qword 32` (pushes vector number)
   - `jmp common_isr_stub`
4. **General Purpose Register Preservation:** `common_isr_stub` pushes all 15 GPRs:
   - `push r15`, `push r14`, `push r13`, `push r12`, `push r11`, `push r10`, `push r9`, `push r8`, `push rbp`, `push rdi`, `push rsi`, `push rdx`, `push rcx`, `push rbx`, `push rax`.
   - Stack pointer `RSP` now points to the 176-byte `InterruptFrame`.
   - `mov rdi, rsp` loads the frame pointer into `RDI` (first argument per SysV AMD64 ABI).
   - `cld` clears direction flag.
   - `call exception_dispatch` invokes C++ dispatcher.
5. **C++ Exception Dispatcher & Timer Handler:**
   - `exception_dispatch(InterruptFrame* frame)` checks `frame->vector == 32`, invoking `handle_timer_interrupt()`.
   - `handle_timer_interrupt()` increments `g_timer_ticks`.
   - `PicManager::send_eoi(0)` writes `0x20` to Master PIC port `0x20` (EOI signaled before preemption so the PIC does not block future interrupts).
   - `Timer::dispatch_tick()` invokes the registered tick hook `Scheduler::on_timer_tick()`.
6. **Timeslice Accounting & Preemption Decision:**
   - `Scheduler::on_timer_tick()` checks `s_running` and reentrancy flag `s_in_scheduler`.
   - Increments `current->ticks_consumed` and `current->ticks_allocated`.
   - Evaluates: `timeslice_expired = (cur->ticks_consumed >= cur->timeslice_ticks)`. At 100 Hz with `DEFAULT_TIMESLICE_TICKS = 2`, each timeslice is 20 ms.
   - If expired, increments `preemption_count`, `s_stats.timer_preemptions`, and calls `Scheduler::schedule()`.
7. **Context Switch Transfer:**
   - `Scheduler::schedule()` transitions `cur->state = ThreadState::Ready` and enqueues `cur` back to `s_ready_queue`.
   - Dequeues `next` runnable thread (or `s_idle_thread` if empty) and sets `next->state = ThreadState::Running`.
   - Clears `s_in_scheduler = false`.
   - Calls `context_switch(&old->context, &next->context)`.
   - `context_switch` saves `old`'s callee-saved registers (`RBX`, `RBP`, `R12`..`R15`, `RFLAGS`, `RSP`), stores return address `[rel .resume]` as `old->rip`, and switches `RSP` to `next->context.rsp`.
8. **Next Thread Execution & Interrupted Thread Resumption:**
   - *If `next` is newly created:* `context_switch` jumps to `thread_bootstrap_trampoline` with `sti`, invoking `entry(arg)`.
   - *If `next` was previously preempted:* `context_switch` returns to `.resume`, executing `ret` into `schedule()`.
   - `schedule()` returns through `on_timer_tick() -> dispatch_tick() -> handle_timer_interrupt() -> exception_dispatch()`.
   - `common_isr_stub` restores all 15 GPRs (`pop rax` .. `pop r15`), discards the vector and dummy error code (`add rsp, 16`), and executes `iretq`.
   - `iretq` pops `RIP`, `CS`, `RFLAGS`, `RSP`, `SS`, instantly resuming the interrupted thread with its exact hardware register state and interrupts re-enabled (`IF = 1`).

---

### 20.2 Exact InterruptFrame Byte-Level Layout
The `InterruptFrame` struct (176 bytes total, 22 qwords) exactly mirrors the assembly stack layout constructed by the x86-64 hardware and `interrupt_stubs.asm`:

| Byte Offset | Register / Field | Size | Written By | Read By | Restored By |
|---|---|---|---|---|---|
| `+0x00` (`+0`) | `RAX` | 8 bytes | `common_isr_stub` (`push rax`) | `exception_dispatch` | `common_isr_stub` (`pop rax`) |
| `+0x08` (`+8`) | `RBX` | 8 bytes | `common_isr_stub` (`push rbx`) | `exception_dispatch` | `common_isr_stub` (`pop rbx`) |
| `+0x10` (`+16`) | `RCX` | 8 bytes | `common_isr_stub` (`push rcx`) | `exception_dispatch` | `common_isr_stub` (`pop rcx`) |
| `+0x18` (`+24`) | `RDX` | 8 bytes | `common_isr_stub` (`push rdx`) | `exception_dispatch` | `common_isr_stub` (`pop rdx`) |
| `+0x20` (`+32`) | `RSI` | 8 bytes | `common_isr_stub` (`push rsi`) | `exception_dispatch` | `common_isr_stub` (`pop rsi`) |
| `+0x28` (`+40`) | `RDI` | 8 bytes | `common_isr_stub` (`push rdi`) | `exception_dispatch` | `common_isr_stub` (`pop rdi`) |
| `+0x30` (`+48`) | `RBP` | 8 bytes | `common_isr_stub` (`push rbp`) | `exception_dispatch` | `common_isr_stub` (`pop rbp`) |
| `+0x38` (`+56`) | `R8` | 8 bytes | `common_isr_stub` (`push r8`) | `exception_dispatch` | `common_isr_stub` (`pop r8`) |
| `+0x40` (`+64`) | `R9` | 8 bytes | `common_isr_stub` (`push r9`) | `exception_dispatch` | `common_isr_stub` (`pop r9`) |
| `+0x48` (`+72`) | `R10` | 8 bytes | `common_isr_stub` (`push r10`) | `exception_dispatch` | `common_isr_stub` (`pop r10`) |
| `+0x50` (`+80`) | `R11` | 8 bytes | `common_isr_stub` (`push r11`) | `exception_dispatch` | `common_isr_stub` (`pop r11`) |
| `+0x58` (`+88`) | `R12` | 8 bytes | `common_isr_stub` (`push r12`) | `exception_dispatch` | `common_isr_stub` (`pop r12`) |
| `+0x60` (`+96`) | `R13` | 8 bytes | `common_isr_stub` (`push r13`) | `exception_dispatch` | `common_isr_stub` (`pop r13`) |
| `+0x68` (`+104`) | `R14` | 8 bytes | `common_isr_stub` (`push r14`) | `exception_dispatch` | `common_isr_stub` (`pop r14`) |
| `+0x70` (`+112`) | `R15` | 8 bytes | `common_isr_stub` (`push r15`) | `exception_dispatch` | `common_isr_stub` (`pop r15`) |
| `+0x78` (`+120`) | `vector` | 8 bytes | `isr_stub_32` (`push qword 32`) | `exception_dispatch` | `add rsp, 16` (discarded) |
| `+0x80` (`+128`) | `error_code` | 8 bytes | `isr_stub_32` (`push qword 0`) | `exception_dispatch` | `add rsp, 16` (discarded) |
| `+0x88` (`+136`) | `RIP` | 8 bytes | CPU Hardware Interrupt | `exception_dispatch` | CPU `iretq` |
| `+0x90` (`+144`) | `CS` | 8 bytes | CPU Hardware Interrupt | `exception_dispatch` | CPU `iretq` |
| `+0x98` (`+152`) | `RFLAGS` | 8 bytes | CPU Hardware Interrupt | `exception_dispatch` | CPU `iretq` |
| `+0xA0` (`+160`) | `RSP` | 8 bytes | CPU Hardware Interrupt | `exception_dispatch` | CPU `iretq` |
| `+0xA8` (`+168`) | `SS` | 8 bytes | CPU Hardware Interrupt | `exception_dispatch` | CPU `iretq` |

Every byte offset has been verified via compile-time `static_assert` and matches the C++ struct definition in `kernel/arch/x86_64/cpu/interrupts.hpp`.

---

### 20.3 Context-Switch ABI and Saved Register Sets
LlamaOS/A maintains a strict separation between function-call context switching and asynchronous interrupt frame preservation:
- **Voluntary Context Switch (`context_switch`):**
  - Governed by the System V AMD64 ABI function call convention.
  - Caller-saved registers (`RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`..`R11`) are volatile across function calls and need not be saved in `ThreadContext`.
  - Callee-saved registers (`RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP`, `RIP`, `RFLAGS`) are preserved in `ThreadContext` (80 bytes, 16-byte aligned).
- **Asynchronous Preemption Interrupt Path (`common_isr_stub`):**
  - An interrupt is asynchronous and can occur between any two machine instructions.
  - Interrupted code may have active values in any register.
  - Therefore, `common_isr_stub` preserves **all 15 General Purpose Registers** in the `InterruptFrame` on the thread's stack. When resumed via `iretq`, every single register is restored to its exact interrupted state.

---

### 20.4 Old/New RSP Handling & Stack Integrity
1. **Stack Association:** In IDT gate 32 (Timer), `ist = 0`. The hardware interrupt frame is pushed directly onto the *current thread's stack*.
2. **Call Frame Nesting:** The ISR call frames (`exception_dispatch` -> `handle_timer_interrupt` -> `dispatch_tick` -> `on_timer_tick` -> `schedule`) all allocate their local stack frames on this same thread stack.
3. **Context Switch RSP:** When `context_switch` is invoked from `schedule()`, it writes the current `RSP` (pointing to the return address inside `schedule()`) into `old->context.rsp`.
4. **Stack Switching:** `context_switch` loads `RSP` from `next->context.rsp`. The CPU is now executing on `next`'s independent stack.
5. **No Stack Frame Overwrite:** The interrupt frame and ISR call chain remain completely untouched on `old`'s stack until `old` is switched back in. When `old` resumes, `context_switch` restores `old->context.rsp`, returns to `schedule()`, unwinds to `common_isr_stub`, and executes `iretq` off `old`'s stack.

---

### 20.5 RIP Resume Semantics
- **New Thread First Activation:** For a freshly created thread, `tcb->context.rip` is initialized to `thread_bootstrap_trampoline`. `context_switch` executes `jmp rax`, entering the trampoline.
- **Preempted Thread Resumption:** When `context_switch` saves context for an interrupted or yielding thread, it stores `lea rax, [rel .resume]` into `old->context.rip`. When rescheduled, `context_switch` executes `jmp rax` to `.resume`, which performs `ret` back into `schedule()`.

---

### 20.6 RFLAGS / IF Semantics & Deadlock Prevention
- **Initial Thread Flag:** `init_thread_context` sets `rflags = 0x202` (Bit 1 reserved = 1, Bit 9 IF = 1).
- **Interrupt Gate Clears IF:** When an interrupt occurs, the x86-64 CPU hardware automatically clears `RFLAGS.IF` before entering the ISR stub.
- **Reentrancy Protection:** Scheduler critical sections are guarded by `save_and_disable_interrupts()` and the `s_in_scheduler` flag.
- **Flag Restoration on Return:**
  - In voluntary switching, `restore_interrupt_state(saved)` restores the original IF.
  - In preemptive switching, unwinding to `iretq` pops the hardware-saved RFLAGS, which restores `IF = 1` immediately as the interrupted thread resumes execution.
- **No Deadlock:** `s_in_scheduler = false` is explicitly cleared before calling `context_switch`, ensuring that the incoming thread is never blocked by a stale reentrancy lock.

---

### 20.7 Register Preservation Proof
The assembly test routine `test_reg_preservation_asm` in `context_switch.asm` validates callee-saved register preservation across **voluntary** context switches:
- Two concurrent threads (`reg_worker1` and `reg_worker2`) load 6 distinct 64-bit non-trivial sentinel values into `RBX`, `RBP`, `R12`, `R13`, `R14`, and `R15`.
- Each worker executes a loop of 50 iterations, calling `schedule_voluntary_yield()` on each iteration.
- After every yield/resume cycle, the assembly code performs an immediate hardware `cmp` of all 6 registers against the original sentinel values. A single-bit difference causes immediate failure.
- In live QEMU execution (`test-context`), all sentinels were preserved across 50 yield invocations per worker (at least 100 yield requests total across both workers; actual context-switch count includes additional switches by the bootstrap thread) without a single bit change (`[REGISTER_PRESERVATION_PASS]`).
- **Claim scope:** This test verifies callee-saved register preservation across the voluntary `context_switch()` code path. PIT preemption also uses `context_switch()` internally, but the register test itself exercises only voluntary yields. Preemptive register preservation relies on the same `context_switch()` callee-save/restore mechanism plus the CPU hardware `iretq` GPR restoration from the `InterruptFrame`.

---

### 20.8 Stack Isolation & Collision Elimination
- Thread stacks reside at `0xFFFFFFFF72000000ULL` (`PDPT[509] PD[400] PT[0]`).
- Each slot occupies 20 KiB (5 pages): 4 KiB unmapped guard page + 16 KiB usable stack mapped `RW NX` supervisor.
- **Collision Immunity:**
  - Separated from IST/TSS stacks (`PDPT[509] PD[384]`) by 32 MiB of unmapped address space.
  - Separated from the kernel direct map and 2 MiB large pages in `PDPT[510]`.
  - Guard page address detection verified via `StackAllocator::is_guard_page_address()`.
- Verified non-overlapping in host unit tests (`test_stack_allocator_math`) and live QEMU (`test-stack`).

---

### 20.9 Preemption Proof & Non-Yielding Workload Verification
In `test-preemption`, two compute threads (`preempt_a` and `preempt_b`) execute tight infinite loops:
```cpp
auto non_yielding_a = [](void*) { while (!s_preempt_stop) s_preempt_a++; };
auto non_yielding_b = [](void*) { while (!s_preempt_stop) s_preempt_b++; };
```
- Neither worker calls `yield()`, `sleep()`, or any blocking primitive. (Verified by textual source inspection; no indirect scheduler calls exist in the lambda bodies.)
- The only mechanism allowing both workers to execute is preemptive timeslice expiration driven by PIT IRQ0.
- Representative live serial execution log:
  `[PREEMPTION_EVIDENCE] Worker A counter=7884777, Worker B counter=10656004, Timer Preemptions=8, Switches=12`
  Both counters exceeded 7,000,000 loop iterations, confirming concurrent CPU time allocation under QEMU emulation.

---

### 20.10 Test Harness Anti-False-Positive Analysis
All 7 live QEMU test cases in `scripts/test_phase5.py` enforce strict verification:
1. **Explicit Token Validation:** Every test checks for a unique pass token emitted only upon successful completion (e.g. `[PREEMPTION_TEST_PASS]`, `[TEST_SCHEDULER_PASS]`).
2. **Numeric Threshold Checks:** Tests require actual runtime metrics (e.g. `s_preempt_a > 1000 && s_preempt_b > 1000 && stats.timer_preemptions > 0`).
3. **Exit Code Verification:** QEMU must exit cleanly with code `33` via ISA debug exit port `0xF4`.
4. **No Static False Positives:** Hardcoded tokens or premature returns are prevented because pass tokens are emitted only within conditionally verified blocks.
5. **Negative Token Rejection:** Serial output is scanned for `KPANIC`, `Triple fault`, `Memory leak detected`, `[FAIL]`, `PANIC`, and `UNHANDLED CPU EXCEPTION`. Presence of any negative token fails the test.
6. **Boot Completeness:** The harness requires `Milestone 5 Accomplished Successfully` in serial output, confirming the kernel reached Phase 5 initialization before executing test logic.

---

### 20.11 Defects Identified During Audit & Patches Applied
During this deep audit, two defects were discovered and patched:

1. **Defect 1: Single-Element `s_terminated_to_reclaim` Bottleneck (Severity: HIGH)**
   - *Issue:* When multiple threads terminated in rapid succession (e.g. in consecutive loop batches), `s_terminated_to_reclaim` was overwritten by subsequent terminated threads before earlier threads could be reclaimed, resulting in leaked stack slots and physical memory.
   - *Root Cause:* `reclaim_deferred_stacks()` only checked a single pointer `s_terminated_to_reclaim`, and `Scheduler::exit()` did not invoke reclamation before overwriting the pointer.
   - *Patch:* Refactored `reclaim_deferred_stacks()` to iterate across the thread pool `s_tcb_pool`, identifying and safely freeing the stacks of all terminated threads not equal to `s_current_thread`. Added a direct call to `reclaim_deferred_stacks()` in `Scheduler::exit()`.
   - *Verification:* Verified with a 9-thread rapid creation/exit stress test across 3 batches. Reclaimed all 40 allocated pages back to baseline (active slots = 1 for idle thread, net allocated pages = 4).

2. **Defect 2: Timeslice Configuration Magic Number & Documentation Discrepancy (Severity: MEDIUM)**
   - *Issue:* `timeslice_ticks` was hardcoded as magic number `2` in `thread.hpp` and `scheduler.cpp`, while documentation referenced 50 ms / 5 ticks.
   - *Patch:* Defined `inline constexpr uint64_t DEFAULT_TIMESLICE_TICKS = 2;` (20 ms at 100 Hz) in `kernel/threading/thread_types.hpp`, used consistently across the scheduler, TCB defaults, and tests. Updated all documentation to reflect the exact 20 ms / 2-tick timeslice.

3. **Defect 3: Architectural Invariant Verification Gap (Severity: LOW)**
   - *Patch:* Added `Scheduler::verify_invariants()` enforcing all 8 scheduler invariants at runtime and integrated invariant assertions into live QEMU tests.

---

### 20.12 Regression Verification Summary
Following all audit patches, the entire test suite was executed:
- `make test-unit`: 100% PASS (524 Phase 5 assertions; **10,378 assertions** across 5 suites + **27 parser test cases** = **10,405 total host checks** across all 6 targets).
- `make test-bios`: 100% PASS (Milestones 1–5 achieved, clean exit code 33).
- `make test-uefi`: 100% PASS (OVMF UEFI firmware, clean exit code 33).
- `make test-faults`: 100% PASS (Hardware `#PF`, `#GP`, and `#DF` verified).
- `make test-phase4-live`: 100% PASS (PCI, Keyboard, Framebuffer verified).
- `make test-phase5-live`: 100% PASS (All 7 live test modes passed).
- CMake / CTest: 100% PASS (6/6 ctest targets passing).

---

### 20.13 Hardware & Architectural Limitations Disclosure
1. **QEMU Emulation vs Real Silicon:** All live verification was executed under QEMU 8.2.2 with standard PIT 8254 emulation and i440fx / OVMF chipsets. Physical hardware with varying TSC drift or APIC routing may require additional calibration in Phase 9.
2. **Single-CPU Architecture:** The Phase 5 scheduler is designed strictly for uniprocessor execution. No SMP spinlocks, per-CPU runqueues, or inter-processor interrupts (IPIs) are present. Multi-core scheduling is explicitly reserved for Phase 9.

---

## 21. Final Verification Sign-Off

All 20 acceptance criteria from the audit directive have been satisfied, as supported by the evidence documented above:
1. IRQ0 delivers periodic interrupts to `Scheduler::on_timer_tick`.
2. Voluntary context switching verified via `Scheduler::yield()`.
3. Preemptive multitasking verified via PIT IRQ0 timeslice expiration.
4. Non-yielding workers both achieve CPU execution (7M+ iterations each in QEMU).
5. RIP/RSP resume semantics verified via `context_switch` and `iretq`.
6. RFLAGS/IF semantics verified (no deadlocks, IF restored on resumption).
7. Callee-saved registers (`RBX`, `RBP`, `R12`..`R15`) preserved across voluntary context switches (QEMU runtime verified).
8. Thread stacks verified disjoint and non-overlapping.
9. Guard pages verified non-present (`P=0`).
10. Terminated thread stacks are safely deferred and reclaimed without use-after-free.
11. ReadyQueue FIFO ordering and duplicate prevention verified.
12. Idle thread CPU halt fallback verified.
13. Test harness anti-false-positive measures confirmed.
14. BIOS boot regression passed.
15. UEFI boot regression passed.
16. CPU exception hardware faults (`#PF`, `#GP`, `#DF`) passed.
17. Phase 4 devices (Keyboard, PCI, Framebuffer) passed.
18. CMake and CTest passed.
19. Make build passed with zero warnings (`-Wall -Wextra -Werror`).
20. Documentation strictly consistent with source implementation.

# PHASE 5 VERIFIED & FROZEN

