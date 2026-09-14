# LlamaOS/A Changelog

All notable changes to LlamaOS/A are documented in this file.

## [0.7.0-alpha] - 2026-09-13 — Phase 7: Minimal Userland Complete

### Added
- **Architectural Ring 3 Hardware Transition** (`kernel/userland/user_entry.asm`):
  - `enter_ring3(uint64_t rip, uint64_t rsp)` via `iretq` dropping privilege level from CPL=0 to CPL=3.
  - Sets `CS = 0x0033`, `SS = 0x002B`, `DS/ES/FS/GS = 0x002B`, `RFLAGS = 0x0000000000000202` (IF=1).
  - Complete zeroing of all 15 general-purpose registers (`RAX`..`R15`) to eliminate kernel data/address leakage.
- **GDT User Descriptors & Dynamic Limit Expansion** (`kernel/arch/x86_64/cpu/gdt.hpp`, `gdt.cpp`):
  - Added slot 5 (`UserData`, selector `0x28`, RPL 3 `0x002B`) and slot 6 (`UserCode`, selector `0x30`, RPL 3 `0x0033`).
  - Implemented `PermanentGdt::install_user_descriptors()` with dynamic GDTR limit expansion (from 39 to 55 bytes) upon process creation, preserving Phase 3 `#GP` fault test selector bounds.
- **User Address Space & Stack Architecture** (`kernel/userland/elf_loader.hpp`, `user_memory.hpp`):
  - Canonical 48-bit lower-half addressing (`0x1000 .. 0x00007FFFFFFFFFFF`) with 4 KiB null-page guard.
  - 16 KiB user stack (4 pages) bounded by an unmapped 4 KiB guard page at `0x00007FFFFFFF9000`.
  - 16-byte initial RSP alignment satisfying System V AMD64 ABI.
  - Hardware W^X protection with `NoExecute` on user stack and data pages.
- **Freestanding ELF64 Executable Loader** (`kernel/userland/elf_loader.hpp`, `elf_loader.cpp`):
  - Validates ELF64 headers (`ELFMAG`, `ELFCLASS64`, `ELFDATA2LSB`, `EM_X86_64`, `ET_EXEC`).
  - Bounds checking, integer overflow defense (`p_memsz >= p_filesz`), lower-half user address confinement, kernel/null overlap rejection.
  - Page-granularity mapping via PMM/VMM with exact W^X permissions and BSS zeroing.
- **User Memory Validator** (`kernel/userland/user_memory.hpp`, `user_memory.cpp`):
  - Boundary and canonical range checking (`is_user_address`, `is_user_range`).
  - Active 4-level page table walking via `g_vmm.translate` verifying `Present`, `User`, and `Writable` page attributes.
- **User Process Subsystem** (`kernel/userland/process.hpp`, `process.cpp`):
  - `Process` structure tracking PID, entry point, user stack top, state, exit code, and backing kernel thread.
  - `ProcessManager` supporting process creation, current process lookup, active counts, and clean termination.
- **Syscall ABI Expansion** (`kernel/syscall/syscall_types.hpp`, `syscall.cpp`):
  - Added `SysExit = 5`.
  - Integrated `UserMemoryValidator::validate_user_buffer` into `SysWriteDebug` for Ring 3 callers.
  - Process-aware `SysGetPid` returning user PID.
- **CPU Exception Containment** (`kernel/arch/x86_64/cpu/interrupts.cpp`):
  - Detected Ring 3 faults (`frame->cs & 3 == 3`) in `#PF` and `#GP` handlers.
  - Safely logs diagnostics and terminates the faulting process via `ProcessManager::terminate_current_process`, preventing kernel panic.
- **Freestanding Minimal Userland Init Program** (`user/init.cpp`, `user/user.ld`, `scripts/bin2c.py`):
  - CPL=3 userland verification emitting `[USER_R3_ENTERED]`, `[USER_SYSCALL_OK]`, `[USER_GETPID_OK]`, `[USER_TICKS_OK]`, `[USER_PREEMPT_OK]`, `[USER_YIELD_OK]`, and `[USER_EXIT_OK]`.
- **Phase 7 Host Unit Regression Suite** (`tests/test_phase7.cpp`):
  - 87 assertions across 6 gates: ELF structures, ELF validation rejection, user address boundaries, user stack geometry, GDT user descriptors, and syscall ABI.
  - Brings total host assertions to 10,531 pure assertions (10,558 total checks including 27 Multiboot2 parser test cases).
- **Phase 7 Live QEMU Automated Verification Suite** (`scripts/test_phase7.py`):
  - 7 automated QEMU test modes: `test-user-entry`, `test-user-syscall`, `test-user-yield`, `test-user-memory`, `test-user-faults`, `test-user-preemption`, and `test-phase7-live`. All passing with exit code 33.

---

## [0.6.0-alpha] - 2026-09-13 — Phase 6: System Call Interface Complete

### Added
- **Architectural MSR Configuration for AMD64 Fast System Calls** (`kernel/syscall/syscall_abi.hpp`, `syscall.cpp`):
  - `IA32_EFER` (`0xC0000080`): Enables `SCE` (System Call Extensions, bit 0).
  - `IA32_STAR` (`0xC0000081`): Configured with `0x0020000800000000ULL` (Kernel CS=0x08, Kernel SS=0x10, UserBase=0x20 giving User SS=0x2B and User CS=0x33).
  - `IA32_LSTAR` (`0xC0000082`): Configured with 64-bit virtual entry point `syscall_entry` (`0xFFFFFFFF80108210`).
  - `IA32_SFMASK` (`0xC0000084`): Configured with `0x0000000000044700ULL` (masks `IF`, `DF`, `TF`, `NT`, `AC` on syscall entry).
  - Subsystem verification method `SyscallManager::verify()` checking all MSR values against expected architectural constants.
- **Low-Level Assembly System Call Entry & Return** (`kernel/syscall/syscall_entry.asm`):
  - 128-byte 16-byte aligned `SyscallFrame` capturing full caller register state: caller RSP, RFLAGS, RIP, RAX, RDI, RSI, RDX, R10, R8, R9, RBX, RBP, R12, R13, R14, R15.
  - Ring 0 vs Ring 3 caller determination via caller RIP sign bit in `RCX`. Ring 3 caller switches stack to `TSS.RSP0` (`0xFFFFFFFF70010004`).
  - Architectural return via `sysretq` (`o64 sysret`) for Ring 3 and `push r11; popfq; jmp rcx` for Ring 0.
- **System Call Types & Error Codes** (`kernel/syscall/syscall_types.hpp`):
  - `SyscallNumber` enum: `SysWriteDebug = 1`, `SysYield = 2`, `SysGetPid = 3`, `SysGetTicks = 4`.
  - Standard error constants: `SYS_SUCCESS = 0`, `SYS_ERR_NOSYS = -1`, `SYS_ERR_INVAL = -2`, `SYS_ERR_FAULT = -3`, `SYS_ERR_PERM = -4`, `SYS_ERR_NOMEM = -5`.
  - Compile-time static assertions for `SyscallFrame` size (128 bytes), alignment (16 bytes), and field offsets.
- **System Call Manager & Central Dispatcher** (`kernel/syscall/syscall.hpp`, `syscall.cpp`):
  - Static switch dispatcher `SyscallManager::dispatch(SyscallFrame* frame)`.
  - `SYS_write_debug`: Emits buffer characters directly to serial COM1 (and Bochs/QEMU debug port `0xE9`). Validates null pointers and lengths up to 1024 bytes.
  - `SYS_yield`: Calls `threading::Scheduler::yield()` to release the CPU cooperatively.
  - `SYS_getpid`: Queries current active thread ID via `Scheduler::current_thread()->id`.
  - `SYS_get_ticks`: Queries PIT timer ticks via `Timer::ticks()`.
  - Rejection of unknown syscall IDs with `SYS_ERR_NOSYS` (-1).
- **Phase 6 Host Unit Test Suite** (`tests/test_phase6.cpp`):
  - 5 test suites with 66 assertions: `SyscallFrame` layout & alignment, syscall numbering & error codes, MSR math & bit encodings, dispatcher routing & boundaries, and register state preservation.
  - Brings total host assertions to 10,444 pure assertions (10,471 total checks including 27 Multiboot2 parser test cases).
- **Phase 6 Live QEMU Automated Verification Harness** (`scripts/test_phase6.py`):
  - 4 automated QEMU test modes: `test-syscall-init` (MSR verification), `test-syscall-dispatch` (real CPU syscall execution, args, return codes), `test-syscall-yield` (cooperative context switching across worker threads), and `test-phase6-live` (comprehensive live suite). All pass with exit code 33.

---

## [0.5.0-alpha] - 2026-09-13 — Phase 5: Process and Threading Subsystem Complete

### Added
- **Ring 0 Supervisor Kernel Thread Model** (`kernel/threading/thread_types.hpp`, `thread.hpp`, `thread.cpp`):
  - Strongly typed `ThreadState` state machine (`Created`, `Ready`, `Running`, `Blocked`, `Terminated`, `Idle`).
  - `ThreadPriority` enumeration (`Idle`, `Low`, `Normal`, `High`, `Realtime`).
  - Strongly typed `ThreadId` with reserved IDs: `tid = 1` for bootstrap thread, `tid = 2` for idle thread, `tid >= 3` for worker threads.
  - `ThreadControlBlock` (TCB) tracking identifier, name, state, priority, callee-saved register context, stack bounds, entry function, scheduling telemetry (time-slice, total ticks, yield count, preemption count), and exit code.
- **Assembly Context Switching Engine** (`kernel/arch/x86_64/cpu/context_switch.asm`):
  - `context_switch(ThreadContext* old_ctx, ThreadContext* new_ctx)` saving and restoring System V AMD64 callee-saved registers (`RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP`, `RIP`, `RFLAGS`).
  - `thread_bootstrap_trampoline` enabling interrupts (`sti`), aligning stack to SysV AMD64 ABI specification (`(RSP + 8) % 16 == 0`), passing arguments in `RDI`, invoking entry function in `R12`, and directing clean thread termination to `Scheduler::exit()`.
  - `test_reg_preservation_asm` for non-trivial callee-saved register preservation verification.
- **Stack Allocator with Hardware Guard Pages** (`kernel/threading/stack_allocator.hpp`, `stack_allocator.cpp`):
  - Dedicated virtual memory window at `0xFFFFFFFF72000000ULL` (`PDPT[509] PD[400]`), isolated from 2 MiB direct-map huge pages, MMIO windows, and Phase 3 IST stacks.
  - 64 concurrent thread stack slots (`MAX_STACK_SLOTS = 64`), 20 KiB (5 pages) per slot.
  - 16 KiB usable stack backed by physical PMM page frames mapped via VMM with `Present=1, Writable=1, NX=1, User=0`.
  - 4 KiB unmapped guard page preceding each usable stack, instantly trapping stack overflows via the Phase 3 `#PF` handler on `IST2`.
- **Bounded Circular Ready Queue** (`kernel/threading/ready_queue.hpp`):
  - Fixed-capacity circular FIFO ring buffer holding up to 64 runnable threads with $O(1)$ push and pop operations.
  - Strict duplicate insertion prevention with search verification before enqueueing.
  - Automatic queue compaction on mid-queue thread removal.
- **Round-Robin Scheduler** (`kernel/threading/scheduler.hpp`, `scheduler.cpp`):
  - Core scheduling APIs: `init()`, `start()`, `schedule()`, `yield()`, `exit()`, `block()`, `unblock()`, `create_thread()`, and `on_timer_tick()`.
  - Dedicated idle thread (`tid = 2`, `"idle"`) running `while (true) halt();`, scheduled exclusively when no runnable worker threads exist.
  - Reentrancy protection via `s_in_scheduler` flag and interrupt disabling guards (`save_and_disable_interrupts()`).
  - Deferred stack reclamation: terminated threads cannot safely free their own stacks while executing on them; stack unmapping and PMM frame deallocation are deferred to `reclaim_deferred_stacks()` upon switching into the succeeding thread.
- **Preemptive Multitasking via PIT IRQ0**:
  - `Timer::set_tick_hook()` integrated into `kernel/arch/x86_64/cpu/timer.cpp`.
  - 100 Hz timer tick accounting (`cur->ticks_consumed++`) with 2 ticks = 20 ms default timeslice (`DEFAULT_TIMESLICE_TICKS = 2`).
  - Preemption triggers context switch and returns cleanly through `iretq` interrupt stack frame.
- **Phase 5 Host Unit Test Suite** (`tests/test_phase5.cpp`):
  - 7 comprehensive test suites verifying 524 assertions: state transitions, context layout and byte offsets, SysV stack alignment, ready queue FIFO and duplicate rejection, stack allocator guard page math, TCB metrics, and scheduler invariants / slot recycling.
- **Phase 5 Live QEMU Automated Verification Harness** (`scripts/test_phase5.py`):
  - 7 isolated automated live test modes: `test-scheduler` (cooperative switching), `test-preemption` (preemptive multitasking), `test-context` (callee-saved register preservation), `test-stack` (guard page & stack isolation), `test-thread-exit` (deferred reclamation), `test-thread-stress` (8 concurrent threads), and `test-phase5-live` (comprehensive suite).

---

## [0.4.0-alpha] - 2026-09-13 — Phase 4: Device and Hardware Abstraction Subsystem Complete

### Added
- **Intel 8042 PS/2 Controller Foundation** (`kernel/drivers/ps2/ps2_controller.hpp`, `ps2_controller.cpp`):
  - Driver management for standard legacy ports `0x60` (Data) and `0x64` (Status / Command).
  - Bounded wait primitives (`wait_input_clear`, `wait_output_full`) preventing hardware deadlocks.
  - Safe command dispatching and output buffer flushing.
  - Configuration byte programming: enables First Port interrupt (bit 0), enables Scan Code Set 2 -> Set 1 translation (bit 6), and clears clock disable (bit 4).
  - Controller self-test (`0xAA` returning `0x55`), first port interface test (`0xAB` returning `0x00`), and dual-channel detection.
- **PS/2 Keyboard Driver & Interrupt Pipeline** (`kernel/drivers/ps2/keyboard.hpp`, `keyboard.cpp`):
  - Interrupt-driven keyboard input via hardware `IRQ1` (Vector 33 / `0x21` on Master PIC).
  - Minimal ISR discipline: zero heap allocation, bounded buffer reads, instant Master PIC EOI signaling (`send_eoi(1)`).
  - Scanning enable command (`0xF4`) and device acknowledgment handshake.
  - Telemetry counters tracking received scancodes and generated key events.
- **Pure Scan Code Set 1 Decoder** (`kernel/drivers/ps2/scancode.hpp`, `scancode.cpp`):
  - Freestanding, host-testable finite state machine converting raw scancodes into structured `KeyEvent` objects.
  - Supports make codes (bit 7 clear) and break codes (bit 7 set).
  - Decodes extended keys preceded by prefix `0xE0` (Arrow keys, Home, End, PageUp, PageDown, Delete, Insert, Right Ctrl, Right Alt).
  - Tracks modifier states across make/break transitions: Left/Right Shift, Left/Right Ctrl, Left/Right Alt, and CapsLock toggle.
  - Full ASCII translation via `key_event_to_ascii()`, handling Shift and CapsLock rules.
- **Bounded Input Event Queue** (`kernel/drivers/ps2/input_queue.hpp`):
  - Strictly Single-Producer Single-Consumer (SPSC) lock-free ring buffer (`InputEventQueue<128>`) with power-of-two indexing.
  - IRQ1 ISR producer (`push()`) and kernel loop consumer (`pop()`).
  - Monotonic head/tail indices with compiler memory fences (`asm volatile("" ::: "memory")`).
  - Removal of shared counter variable to eliminate single-core interrupt race conditions.
  - Deterministic overflow behavior: drops event upon capacity exhaustion and increments `dropped_count()` telemetry.
- **PCI Configuration Space Bus Enumeration** (`kernel/drivers/pci/pci.hpp`, `pci.cpp`):
  - Standard I/O port `0xCF8` (Config Address) and `0xCFC` (Config Data) access primitives.
  - Address encoding helper `make_config_address(bus, dev, func, offset)`.
  - Full generic scanning across all 256 buses (`0..255`), devices `0..31`, and multi-function device enumeration across functions `0..7`. Fast slot-skipping on empty slots (`0xFFFF`/`0x0000`).
  - Device identification: Vendor ID, Device ID, Class Code, Subclass, Prog IF, Revision, Header Type.
  - Non-destructive, read-only Base Address Register (BAR0..BAR5) decoding:
    - I/O Space BARs (bit 0 = 1, base address masked to 4-byte boundary).
    - 32-bit Memory Space BARs (bit 0 = 0, type 00).
    - 64-bit Memory Space BARs (type 10, combines lower and upper 32-bit registers). Verified via synthetic host tests; transparently declared as not observed in live QEMU due to default i440fx topology.
    - Prefetchable memory flag extraction (bit 3).
  - Human-readable PCI class string formatter covering Host Bridges, ISA Bridges, Storage (IDE/SATA/NVMe), Network, Display/VGA, and System Peripherals.
- **Linear Framebuffer Subsystem** (`kernel/drivers/framebuffer/framebuffer.hpp`, `framebuffer.cpp`):
  - Multiboot2 graphical metadata parser and validator (`validate_metadata`).
  - Strict elimination of silent truncation: introduced `calculate_mapping_plan()` validating framebuffer size against `MMIO_WINDOW_SIZE` (512 MiB / 131,072 pages) with deterministic rejection on overflow.
  - Physical base address page-alignment normalization (`aligned_paddr`, `page_offset`, preserving virtual offset without unmapped holes).
  - VMM higher-half Page Directory 1 entries 256..511 preserved as unmapped during bootstrap, allowing clean dynamic 4-level page table allocation into `MMIO_WINDOW_START` (`0xFFFFFFFFA0000000`) without `HugePageCollision`.
  - Enforced W^X security permissions: `Present=1, Writable=1, NX=1, User=0` (Supervisor Non-Executable).
  - Color pixel format detection (BGRA8888, RGBA8888, ARGB8888, RGB888, BGR888).
  - Checked 2D graphics primitives: `is_pixel_in_bounds` performing checked arithmetic preventing coordinate wrap-around, `put_pixel`, `fill_rect`, `clear`.
- **Unified Kernel Console** (`kernel/drivers/console/console.hpp`, `console.cpp`):
  - Unified output abstraction multiplexing characters simultaneously across Serial (`COM1`, `0xE9`) and VGA Text Buffer (`0xB8000`).
  - Consistent cursor and color control across physical outputs.
- **Device Abstraction Layer & Device Registry** (`kernel/drivers/devices/device_registry.hpp`, `device_registry.cpp`):
  - Strongly typed `DeviceInfo` abstraction tracking device name, type (`Console`, `SerialPort`, `VgaDisplay`, `Framebuffer`, `Ps2Controller`, `Keyboard`, `PciDevice`, `Timer`, `Pic`), state, and hardware resources (I/O port, MMIO base, IRQ).
  - Device registry table storing detected devices with search by name and index queries.
- **Hardware Discovery Boot Report** (`kernel/drivers/devices/hardware_report.hpp`, `hardware_report.cpp`):
  - One-time boot summary logging CPU, Console, Framebuffer geometry, Interrupt Controller, PIT timer status, PS/2 controller, PS/2 keyboard, all discovered PCI devices with BARs, and total registered subsystem devices.
- **Phase 4 Host Unit Regression Suite** (`tests/test_phase4.cpp`):
  - 9 comprehensive test suites containing 8,810 assertions verifying Set 1 scancodes, make/break transitions, modifier key logic, CapsLock toggle, extended `0xE0` prefixes, bounded lock-free SPSC circular queue FIFO ordering, monotonic wrap-around across 100 cycles, alternating push/pop across 1,000 iterations, drop telemetry, PCI config address construction across all buses (0 and 255), BAR decoding (I/O, Mem32, Mem64 prefetchable and high-memory synthetic tests), framebuffer metadata validation (normal, 4K, 8K, padding, unaligned base, overflow defense), mapping plan capacity & silent truncation defense, checked coordinate boundary access, and device registry operations.
- **Live QEMU Test Automation** (`scripts/test_phase4.py`, `scripts/test_interactive.py`):
  - `test-pci`: Automated verification of PCI bus device discovery in QEMU.
  - `test-keyboard`: Automated verification of keyboard scancode decode and queue pipeline.
  - `test-framebuffer`: Automated verification of linear framebuffer primitive drawing.
  - `test-interactive`: Live typing verification injecting keystrokes via QEMU monitor socket and validating serial echo.
- **Build System Integration**:
  - Targets `test-phase4`, `test-phase4-live`, and `test-interactive` integrated in `Makefile` and `CMakeLists.txt` / `ctest`.

---

## [0.3.0-alpha] - 2026-09-13 — Phase 3: CPU Descriptors, Exceptions & Interrupt Subsystem Complete

### Added
- **Permanent Long-Mode Global Descriptor Table (GDT)** (`kernel/arch/x86_64/cpu/gdt.hpp`, `gdt.cpp`, `gdt_asm.asm`):
  - Replaces bootstrap descriptor tables with permanent 64-bit long-mode GDT.
  - Documented selector constants: `Null` (0x00), `KernelCode` (0x08), `KernelData` (0x10), and `Tss` (0x18).
  - Explicit descriptors for 64-bit kernel code (`0x00AF9A000000FFFF`), 64-bit kernel data (`0x00CF92000000FFFF`), and 16-byte TSS system descriptor (`encode_tss_descriptor`).
  - Far-return segment reloading routine (`reload_segments` in NASM) refreshing `CS`, `DS`, `ES`, `SS`, `FS`, and `GS`.
  - Task Register loading via `load_task_register` (`ltr`) and verification via `read_task_register` (`str`) and TSS Busy bit inspection (bit 41).
  - VMM virtual page mapping validation confirming GDT table is mapped with RW NX permissions in higher-half space.
- **Task State Segment (TSS) & Dedicated IST Infrastructure** (`kernel/arch/x86_64/cpu/tss.hpp`, `tss.cpp`):
  - Standard 104-byte 64-bit TSS structure with `rsp0` and `ist1..ist7` entries.
  - Dedicated, isolated IST stacks allocated through PMM (`g_pmm.alloc_pages`) and mapped via VMM (`g_vmm.map_page`):
    - `IST1` (16 KiB / 4 pages): Dedicated Double Fault (#DF) stack.
    - `IST2` (16 KiB / 4 pages): Dedicated Page Fault (#PF) stack.
    - `IST3` (16 KiB / 4 pages): Dedicated NMI / Machine Check (#MC) / Critical exception stack.
    - Dedicated TSS page (4 KiB) allocated via PMM and mapped with RW NX permissions.
  - Hardware-enforced guard regions: 4 KiB unmapped virtual guard pages below each IST stack catching stack overflows deterministically.
  - Numerical alignment (16-byte aligned stack pointers) and distinct non-zero stack verification.
- **Interrupt Descriptor Table (IDT)** (`kernel/arch/x86_64/cpu/idt.hpp`, `idt.cpp`):
  - Full 256-entry x86-64 IDT with strongly typed 16-byte `IdtEntry` gate descriptors.
  - Configures CPU exception gates (#DE through #CP, vectors 0..21) with `KernelCode` selector (0x08) and kernel-only DPL (0).
  - Wires dedicated IST indices: #DF -> IST1, #PF -> IST2, #NMI -> IST3, #MC -> IST3.
  - Configures hardware IRQ and spurious interrupt gates: IRQ0 (Timer, vector 32), Master Spurious (IRQ7, vector 39), Slave Spurious (IRQ15, vector 47), and Spurious APIC (vector 255).
  - `IdtManager::verify()` checking IDTR base, limit, handler `.text` section bounds, present bits, and uninstalled gate safety.
- **Low-Level ISR Assembly Stubs & Normalization** (`kernel/arch/x86_64/cpu/interrupt_stubs.asm`):
  - Separate macros distinguishing vectors that push an error code (8, 10..14, 17, 21) from vectors that do not (pushing dummy 0 error code).
  - Uniform 176-byte `InterruptFrame` structure saving all 15 general-purpose registers (`rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8..r15`), `vector`, `error_code`, `rip`, `cs`, `rflags`, `rsp`, and `ss`.
  - System V AMD64 ABI alignment compliance, `cld` enforcement, and clean restoration before `iretq`.
- **Central Exception Dispatcher & Diagnostic Subsystem** (`kernel/arch/x86_64/cpu/interrupts.hpp`, `interrupts.cpp`):
  - Central exception router with full register dump diagnostics.
  - **Page Fault Handler (#PF, Vector 14)**: executes on dedicated IST2 stack, reads `CR2` and `CR3`, decodes architectural error code flags (`Present`, `Write/Read`, `User/Supervisor`, `Reserved Bit`, `Instruction Fetch / NX`, `Protection Key`, `Shadow Stack`, `SGX`), and halts with deterministic kernel panic.
  - **Double Fault Handler (#DF, Vector 8)**: executes on dedicated IST1 stack, logs containment diagnostics, and halts CPU safely without cascading triple faults.
  - **General Protection Fault Handler (#GP, Vector 13)**: decodes error code bits (`External`, `IDT`, `LDT/GDT`, `Selector Index`), logs diagnostic context, and panics deterministically.
  - **Debug and Breakpoint (#BP / INT3)**: captures breakpoint exceptions, increments breakpoint counter, logs RIP/CS/RFLAGS/RSP, and returns cleanly to resuming code.
- **Programmable Interrupt Controller (Dual 8259 PIC & APIC Foundation)** (`kernel/arch/x86_64/cpu/pic.hpp`, `pic.cpp`):
  - Hardware reconnaissance: CPUID APIC capability detection and `IA32_APIC_BASE` MSR (0x1B) interrogation (base physical address, global enable, BSP status).
  - Dual 8259 PIC remapping (Master: vectors 0x20..0x27, Slave: vectors 0x28..0x2F).
  - Fail-closed initial masking of all 16 IRQ lines.
  - End-of-Interrupt (EOI) signaling (`send_eoi`).
  - Spurious interrupt detection and discrimination for IRQ7 and IRQ15 with in-service register (ISR) checking.
- **Timer Interrupt Foundation** (`kernel/arch/x86_64/cpu/timer.hpp`, `timer.cpp`):
  - PIT 8254 periodic timer initialization at 100 Hz (Channel 0, Mode 2 rate generator).
  - Monotonic tick counter (`g_timer_ticks`) incremented by IRQ0 interrupt handler with PIC EOI.
  - Real CPU interrupt control API: `interrupts_enabled`, `enable_interrupts` (`sti`), `disable_interrupts` (`cli`), `save_and_disable_interrupts`, and `restore_interrupt_state`.
  - Deterministic hardware verification: bounded wait confirming `ticks_after > ticks_before` upon enabling interrupts.
- **Host Unit Regression Test Suite** (`tests/test_descriptors.cpp`):
  - 7 comprehensive tests with 64 assertions verifying selector constants, GDT code/data descriptor encoding, TSS descriptor encoding and limit/base reconstruction, IDT gate bitfield encoding (interrupt vs trap, DPL, IST), `InterruptFrame` struct offsets, exception metadata table, and error code decoders for Page Faults and GPFs.
  - Integrated into GNU `Makefile` and `CMakeLists.txt` / `ctest`.
- **Bootloader & QEMU Integration Verification**:
  - Automated BIOS and UEFI boot test validation confirming GDT, TSS, IST, IDT, Breakpoint dispatch, PIC remapping, and timer interrupt delivery.

---

## [0.2.0-alpha] - 2026-09-13 — Phase 2: Memory Management Subsystem Complete

### Added
- **Strongly-Typed Memory Abstractions** (`kernel/memory/memory_types.hpp`):
  - `PhysicalAddress`: explicit hardware address wrapper with checked alignment verification (`is_page_aligned`, `align_up`, `align_down`, `align_up_checked`), direct-map detection (`is_direct_mapped`), range offsets, relational operators, and higher-half mapping translation.
  - `VirtualAddress`: canonical 48-bit address validation, higher-half detection, pointer accessors, checked alignment (`align_up_checked`), and 4-level page directory indexing (`pml4_index`, `pdpt_index`, `pd_index`, `pt_index`, `page_offset`).
  - `PageFrameNumber` & `PageCount`: typed representations for frame indices and page amounts with overflow-safe byte conversion arithmetic (`from_bytes` guarded against `UINT64_MAX` wrapping).
  - `PageFlags`: architectural x86-64 bitmask enum class supporting `Present`, `Writable`, `User`, `WriteThrough`, `CacheDisable`, `Accessed`, `Dirty`, `HugePage`, `Global`, and `NoExecute` (NX bit 63).
- **Virtual Memory Space Architecture** (`kernel/memory/memory_layout.hpp`):
  - Formally documents canonical 48-bit addressing layout with named architectural constants: `USER_SPACE_START`/`END`, `HIGHER_HALF_BASE`, `KERNEL_BASE_VIRTUAL`, `DIRECT_MAP_PHYS_LIMIT` (2 GiB), `KERNEL_GUARD_REGION`, `KERNEL_HEAP_START`/`END`, `VMM_TABLE_POOL_START`/`END`, and `MMIO_WINDOW_START`/`END`.
- **Physical Memory Manager (PMM)** (`kernel/memory/pmm.hpp`, `pmm.cpp`):
  - 4 KiB physical page frame bitmap allocator with fail-closed default (entire bitmap initially filled with 1s).
  - Normalization of Multiboot2 memory map entries with strict reservation precedence.
  - Strict re-reservation overrides protecting Page 0 (NULL pointer trap), low memory (`0x00000000 .. 0x00100000`), kernel physical image, Multiboot2 info block, PMM bitmap buffer, and firmware reserved/ACPI/NVS regions.
  - Zero-initialization of allocated physical frames within the higher-half direct map for hygiene.
  - Double-free detection guard rejecting attempts to deallocate unallocated frames.
  - Strict rejection of attempts to deallocate reserved frames (firmware, low memory, kernel, bitmap).
  - Counter underflow protection on allocated frame count.
  - Multi-page contiguous allocation algorithm (`alloc_pages`, `free_pages`) with 64-bit integer overflow protection and wrap-around searching.
  - Calculation and reporting of largest contiguous free page run (`largest_contiguous_free_pages`).
  - Test-only fault injection hook (`set_force_alloc_failure`) for controlled failure verification.
  - Safe physical memory management beyond 4 GiB without 32-bit truncation or integer overflow.
- **Reserved Memory Tracking & Disjoint Interval Normalization** (`kernel/memory/reserved_regions.hpp`, `reserved_regions.cpp`):
  - Unified registration table for physical memory regions with sorting, adjacent merging, and fail-closed reservation queries.
  - Strict architectural precedence: any reservation strictly overrides availability.
  - Disjoint interval subtraction arithmetic: all reserved intervals are merged, and then subtracted from all usable intervals (clipping and splitting), guaranteeing zero overlap across all physical memory intervals.
  - Conservative sub-page alignment: reserved regions expand outward to whole pages (`align_down` start, `align_up` end); available regions shrink inward (`align_up` start, `align_down` end).
- **Virtual Memory Manager (VMM) Foundation & Dynamic Migration** (`kernel/memory/vmm.hpp`, `vmm.cpp`):
  - 4-level page table abstraction (`PageTableEntry`, `PageTable`) for x86-64 Long Mode.
  - Dynamic page table allocation through PMM for unmapped intermediate levels (PML4, PDPT, PD, PT).
  - Autonomous migration from early bootstrap page tables (`early_pml4`) to dynamically allocated root PML4.
  - **Live Hardware CR3 Page Table Walking** (`walk_live_cr3`): inspects active CPU CR3 hierarchy and validates raw 64-bit PTE bitfields (`Present`, `Writable`, `User`, `NoExecute`).
  - **Hardware Null-Page Protection & Lower-Half Verification**: `new_pml4->entries[0]` unmapped, confirming `PML4[0..255] == 0x0` with full unmapping of lower canonical memory.
  - **Granular W^X Section Permissions**: splits initial 2 MiB page into 512 x 4 KiB pages via Level 1 PT0, enforcing:
    - `.text` = Executable, Read-Only (RX: `Present`, not Writable, not NX)
    - `.rodata` = Read-Only, Non-Executable (R: `Present | NoExecute`, not Writable)
    - `.data` = Writable, Non-Executable (RW NX: `Present | Writable | NoExecute`)
    - `.bss` & Kernel Stack = Writable, Non-Executable (RW NX: `Present | Writable | NoExecute`)
    - Low memory (0..1 MiB in higher-half) = `Present | Writable | NoExecute`
  - High-half GDTR reload (`sgdt`/`lgdt`) prior to CR3 switch to ensure CPU segment registers access GDT in higher-half.
  - Verification of `IA32_EFER.NXE` (bit 11) before relying on No-Execute bit 63.
  - `map_page`: maps 4 KiB physical frames with architectural permissions.
  - `unmap_page`: unmaps virtual pages, clears PTE, and invalidates TLB via `invlpg`.
  - `translate`: hardware page table walk supporting standard 4 KiB pages and 2 MiB large pages.
  - Controlled fault injection handling: returns `VmmStatus::OutOfMemory` cleanly on PMM allocation failure.
  - Real-time RDTSC performance latency benchmarking for PMM and VMM core primitives.
- **Kernel Allocation Foundation** (`kernel/memory/kalloc.hpp`, `kalloc.cpp`):
  - Early page-granularity virtual memory allocation (`alloc_kernel_pages`, `free_kernel_pages`).
  - Interface stubs for Phase 3 heap subsystem (`kmalloc`, `kfree`).
- **Decoupled Host Unit Regression Test Suites** (`tests/test_pmm.cpp`, `tests/test_vmm.cpp`, `tests/test_parser.cpp`):
  - `test_pmm`: 17 tests, 365 assertions verifying typed address arithmetic, alignment, PMM bitmap simulation, contiguous runs, high-memory (>4 GiB) host safety, overflow guards, double/unaligned/invalid free rejection, and the 6 interval-subtraction memory normalization scenarios.
  - `test_vmm`: 10 tests, 611 assertions verifying canonical bounds, 4-level decomposition, bitmasks, PTE encoding, 4096-byte alignment, mapping collisions, repeated map/unmap cycles, NX/User conversion, W^X constraints, 2MB huge page collision detection, and invalid address rejection.
  - `test_parser`: 27 tests verifying Multiboot2 parsing, tag validation, ACPI, EFI, and command-line tokenization.
  - Total host unit suite: 54 tests, 976+ assertions passing with zero failures.
- **Audited Kernel Boot Self-Tests** (`kernel/kernel_main.cpp`, `pmm.cpp`, `vmm.cpp`):
  - Comprehensive PMM self-test suite (13/13 deterministic gates): Initial free count sanity, aligned single alloc, bit allocation check, clean free, double-free rejection, reserved frame deallocation rejection (frame 0, page 0, low RAM, kernel image, MB2 block, bitmap), multi-page contiguous alloc/verify/free, allocation exhaustion and fault injection, no reserved overlap, bitmap boundary testing, first/last/out-of-bounds frame limits, arithmetic overflow, global accounting balance equation, and RDTSC latency measurement.
  - Comprehensive VMM self-test suite (14/14 deterministic gates): Live CR3 walk with raw 64-bit PTE bit inspection for Kernel `.text` RX, `.rodata` R, stack and `.data`/`.bss` RW NX, Null-Page Protection confirmation, `PML4[0..255] == 0` lower-half unmapped verification, dynamic page allocation and mapping, page table translation, physical memory cross-verification, re-mapping collision rejection, unmapping and TLB invalidation, multi-page mapping sequence, non-canonical address rejection, fault injection handling, and RDTSC latency measurement.
- **Compiler Stack Protector Configuration** (`Makefile`, `CMakeLists.txt`):
  - Fixed implicit TLS `%gs:0x28` dependency in GCC by adding `-mstack-protector-guard=global`, binding `-fstack-protector-strong` directly to the kernel's global `__stack_chk_guard` canary symbol. Prevents page faults when lower-half identity mapping is removed.

---

## [0.1.2-audit] - 2026-09-12 — Phase 1 Hardening, Security, Correctness, and Reliability Audit Complete

### Fixed & Hardened
- **Multiboot2 Parser Correctness & Security** (`kernel/arch/x86_64/boot/multiboot2.cpp`, `multiboot2.hpp`):
  - Strict terminating tag validation: enforced `TagType::End (0)` tag size == 8 bytes, rejecting malformed or truncated end tags.
  - Duplicate tag suppression: prevented duplicate `Cmdline`, `BootLoaderName`, `BasicMemInfo`, `Mmap`, `Framebuffer`, `AcpiOld`, `AcpiNew`, and `Efi64` tags from corrupting state or double-counting RAM.
  - Memory-map security: verified `entry_version == 0`, `24 <= entry_size <= 1024`, detected trailing partial entries (`mmap_truncated = true`), and safely supported future entries larger than 24 bytes without reading incomplete entries.
  - Total usable RAM accumulator protected against 64-bit integer overflow with saturation arithmetic and warning flag.
  - 64-bit address and size preservation: eliminated truncation in memory map and framebuffer representations.
  - Framebuffer validation: verified minimum tag size (32 bytes), max dimensions (7680x4320), minimum and maximum pitch sanity, format-specific lengths (36 for Indexed, 38 for Direct RGB), multiplication overflow protection, and address overflow protection.
  - ACPI representation correctness: eliminated confusion between the Multiboot2 copy virtual pointer (`mb2_rsdp_copy_addr`) and physical firmware memory addresses (`rsdt_physical_address`, `xsdt_physical_address`). Validated `"RSD PTR "` signature and checksums (20-byte and extended).
  - EFI representation: explicitly tracked `present`, `valid`, `is_physical`, `is_firmware_owned`, and 64-bit physical system table address.
- **Early Bootstrap & Memory Mapping Bug Fix** (`kernel/arch/x86_64/boot/boot.asm`):
  - Fixed off-by-one table clearing bug in `setup_early_page_tables`: now zeroes all 6 tables `(6 * 4096) / 4` dwords (`early_pml4`, `early_pdpt_identity`, `early_pdpt_kernel`, `early_pd_identity`, `early_pd_kernel`, `early_pd_kernel2`) instead of only 5.
- **Linker Script & Freestanding C++ Runtime** (`kernel/arch/x86_64/linker.ld`, `kernel/core/runtime.cpp`, `kernel/core/types.hpp`):
  - Added `.init_array` and `.ctors` mapping with `_init_array_start` and `_init_array_end` in `.rodata`.
  - Implemented `call_global_constructors()` to execute static constructors before kernel subsystems.
  - Added freestanding placement new/new[] and deterministic panicking `operator delete` stubs.
  - Added overflow-safe `align_up` and `align_up_checked` arithmetic helpers in `types.hpp`.
  - Defined `UINTPTR_MAX` and `INTPTR_MAX` constants.
- **Kernel Logging & Formatted Printing Hardening** (`kernel/core/kprint.cpp`):
  - Implemented `print_padded_dec()` for width and zero-padding support in `%02u`, `%d`, etc.
  - Fixed default hex width for 64-bit integers (`%lx` and `%llx` now default to 16 hex digits instead of truncating to 8).
  - Eliminated undefined behavior in `kprint_signed` on `INT64_MIN` negation.
- **Audited Runtime Self-Test Suite** (`kernel/kernel_main.cpp`):
  - Category 1: CPU Architecture & Security Extensions (LM, NX, PAE, CR0, CR4, EFER).
  - Category 2: Freestanding Memory Primitives (memcpy, memmove forward/backward with overlap, memset, strcmp).
  - Category 3: Higher-Half Runtime Execution & MMU Paging Walk (RIP in `.text`, RSP in kernel stack, CS/DS, CR3 -> PML4[511] -> PDPT[510] -> PD[0] 2MB page).
  - Category 4: Linker Section Layout & Stack Sanity (physical/virtual mapping consistent, 64 KiB stack).
  - Category 5: Bootloader Protocol & Bounds Integrity (Multiboot2 valid, magic verified).
  - Category 6: Command-Line Tokenizer & Boot Mode Integrity (exact token matching, no false positives).
  - Category 7: Physical Memory Map & Usable RAM Integrity (bounds, overflow safety, usable RAM verified).
  - Category 8: Framebuffer Metadata Validation (when present).
  - Category 9: ACPI RSDP Metadata Validation (when present).
  - Category 10: Stack Protector Security Canary Active (`__stack_chk_guard != 0`).
- **Automated Test Harness & Host Regression Tests** (`scripts/test_boot.py`, `scripts/run_qemu.sh`, `tests/test_parser.cpp`):
  - Upgraded `scripts/test_boot.py` to run both BIOS and UEFI boot tests sequentially with OVMF autodetection, checking mandatory tokens and debug-exit code 33.
  - Added headless fallback in `scripts/run_qemu.sh` when `DISPLAY` is not set.
  - Expanded host-side regression test suite to 27 unit tests in `tests/test_parser.cpp`.
  - Synchronized `Makefile` and `CMakeLists.txt` with `test`, `test-bios`, and `test-uefi` targets.

---

## [0.1.0-alpha] - 2026-09-12 — Phase 1: Bootable Foundation


### Added
- **x86-64 Higher-Half Linker Configuration** (`kernel/arch/x86_64/linker.ld`):
  - Physical load address: `0x00100000` (1 MiB).
  - Virtual higher-half base address: `0xFFFFFFFF80000000` (-2 GiB).
  - Segment separation for `.boot`, `.text`, `.rodata`, `.data`, `.bss`, and `GNU_STACK`.
- **Early Bootstrap & Long Mode Transition** (`kernel/arch/x86_64/boot/boot.asm`):
  - Multiboot2 compliant header supporting framebuffer, console flags, and information requests.
  - 32-bit protected mode entry point `_start`.
  - CPUID presence check and x86-64 Long Mode capability verification.
  - SSE/FPU activation (`CR0.MP`, `CR4.OSFXSR`, `CR4.OSXMMEXCPT`).
  - 4-level paging setup (PML4, PDPT, PD) identity mapping 1 GiB and higher-half mapping 2 GiB with 2 MiB large pages.
  - 64-bit GDT configuration and far-return jump into Long Mode.
  - Higher-half stack allocation (64 KiB).
- **Freestanding Core Library** (`kernel/core/`):
  - `types.hpp`: Freestanding fixed-width integers, pointers, and memory alignment utilities.
  - `string.hpp` & `string.cpp`: Freestanding `memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`, `strncmp`.
  - `kprint.hpp` & `kprint.cpp`: Formatted printing (`kprintf`, `klog`) with color-coded severity levels (`DEBUG`, `INFO`, `WARN`, `ERROR`, `PANIC`).
  - `panic.hpp` & `panic.cpp`: Deterministic panic handler with control register dump (`CR0`, `CR2`, `CR3`, `CR4`, `RFLAGS`, `EFER`) and QEMU test exit.
  - `runtime.cpp`: Freestanding C++ ABI runtime hooks and `__stack_chk_fail` canary protector.
- **Device Drivers** (`kernel/drivers/`):
  - `serial.hpp` & `serial.cpp`: 16550 UART driver (COM1, 115200 8-N-1) and mirrored Bochs/QEMU port `0xE9`.
  - `vga.hpp` & `vga.cpp`: 80x25 text-mode console with cursor control, color support, and scrolling.
- **Hardware & Boot Reconnaissance** (`kernel/arch/x86_64/`):
  - `multiboot2.hpp` & `multiboot2.cpp`: Multiboot2 tag parser (memory maps, framebuffer, command line, loader name).
  - `cpu.hpp` & `cpu.cpp`: CPUID feature enumeration, processor vendor and brand identification.
  - `io.hpp`: Inlined port I/O primitives (`inb`, `outb`, `inw`, `outw`, `inl`, `outl`).
  - `msr.hpp`: Model-Specific Register access routines (`rdmsr`, `wrmsr`).
- **Kernel Main Entry** (`kernel/kernel_main.cpp`):
  - Recognizable system ASCII banner and build metadata display.
  - Automated diagnostic self-test suite (CPUID, memory primitives, Multiboot2 handshake, higher-half virtual bounds).
  - QEMU ISA debug-exit signal on test completion (`0xF4 -> 0x10`).
- **Build and Test Automation**:
  - `Makefile`: Reproducible targets for `kernel`, `iso`, `test`, `qemu`, `qemu-uefi`, `qemu-gdb`.
  - `CMakeLists.txt`: Full CMake build and test integration.
  - `scripts/test_boot.py`: Autonomous boot test harness validating serial tokens and exit codes.
  - `boot/grub/grub.cfg`: GRUB configuration for hybrid BIOS and UEFI boot.
- **Documentation**:
  - `README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/BUILDING.md`, `docs/DEBUGGING.md`, `docs/CHANGELOG.md`.
