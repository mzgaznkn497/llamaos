# LlamaOS/A Development Roadmap

This document establishes the strategic 13-phase development roadmap for LlamaOS/A. Every milestone builds upon previously verified architectural components.

---

## Phase Status Summary

| Phase | Subsystem | Status | Verification Mechanism |
|---|---|---|---|
| **Phase 0** | Project Reconnaissance | **Completed** | Host environment & toolchain detection |
| **Phase 1** | Bootable Foundation | **Completed** | Hybrid ISO, Higher-Half Long Mode, Banner in QEMU |
| **Phase 2** | Memory Management Subsystem | **Completed** | PMM (Bitmap), VMM (4-Level), Safe typed page mapping, Host regression tests, QEMU BIOS+UEFI boot tests |
| **Phase 3** | CPU and Interrupt Subsystem | **Completed** | Permanent GDT, TSS, Dedicated IST Stacks (#DF, #PF, #MC), IDT (256 gates), Central Exception Dispatcher, Fault Diagnostics, Dual 8259 PIC Remap, PIT Timer, Interrupt Control API, Host & QEMU Tests |
| **Phase 4** | Device & Hardware Abstraction | **Completed** | PS/2 8042 Controller, PS/2 Keyboard Driver (IRQ1, Vector 0x21), Set 1 Scancode Decoder, Bounded Input Event Queue, PCI Configuration Space Bus Enumeration, Linear Framebuffer Subsystem, Unified Console, Device Registry, Hardware Discovery Report, Host Unit & Live QEMU Tests |
| **Phase 5** | Process & Threading Subsystem | **Completed** | Kernel Threads, Preemptive Scheduler, Context Switch, SysV Stack Align, Guard Pages, 7 Host Test Suites, 7 Live QEMU Tests |
| **Phase 6** | System Call Interface | **Completed** | `SYSCALL`/`SYSRET` ABI, MSR Configuration, 128-byte SyscallFrame, Dispatcher, 4 Live QEMU Tests, 66 Host Assertions |
| **Phase 7** | Minimal Userland | **Completed** | Ring 3 CPL=3 (`iretq`), User GDT (`0x2B`/`0x33`), ELF64 Loader, User Stack (16 KiB + Guard), User Memory Validation, Syscall Roundtrip, Preemption, 87 Host Assertions, 7 Live QEMU Tests |
| **Phase 8** | Virtual Filesystem (VFS) | *Planned (Next)* | VFS Node Tree, Mount Table, Initial Filesystem (TarFS/Ext2) |
| **Phase 9** | Symmetric Multiprocessing (SMP) | *Planned* | ACPI MADT, AP Bootstrapping, Per-CPU State, Spinlocks |
| **Phase 10**| Networking Stack | *Planned* | VirtIO-Net / E1000, ARP, IPv4, ICMP, UDP, TCP Sockets |
| **Phase 11**| Security Architecture | *Planned* | Privilege Separation, Capabilities, Resource Limits, Audit |
| **Phase 12**| Storage & Package Management | *Planned* | Block Device Caching, Partition Tables, Package Format |
| **Phase 13**| Graphical Subsystem | *Planned* | Linear Framebuffer Compositor, Windowing, Font Rendering |

---

## Detailed Milestone Descriptions

### Phase 1 — Bootable Foundation (COMPLETED)
- Higher-half x86-64 kernel memory layout linked at `-2 GiB` (`0xFFFFFFFF80000000`).
- Multiboot2 header with tags for framebuffer, information request, and console flags.
- NASM 32-bit bootstrap verifying Multiboot2 magic, CPUID support, and Long Mode capability.
- Early 4-level paging mapping 2 GiB physical memory with 2 MiB large pages.
- Transition into 64-bit Long Mode with proper segment register initialization and higher-half stack.
- Freestanding C++20 core (`types.hpp`, `string.hpp`, `kprint.hpp`, `panic.hpp`, `runtime.cpp`).
- 16550 UART Serial driver (`COM1`, port `0x3F8`) and mirrored Bochs/QEMU debug output (`0xE9`).
- Text-mode 80x25 VGA driver (`0xB8000`) with color controls and cursor management.
- Multiboot2 parser for command line, loader name, memory maps, and framebuffer descriptors.
- CPUID hardware reconnaissance identifying CPU vendor, brand, topology, and feature flags.
- Automated QEMU boot test harness (`scripts/test_boot.py`) with ISA debug exit (`0xF4`).
- Dual build systems: GNU `Makefile` and `CMakeLists.txt`.
- Verified boots under both legacy BIOS and OVMF UEFI firmware.

### Phase 2 — Memory Management Subsystem (VERIFIED & AUDITED)
- **Strongly-Typed Memory Primitives** (`kernel/memory/memory_types.hpp`):
  - `PhysicalAddress`, `VirtualAddress`, `PageFrameNumber`, `PageCount`, `PageFlags`.
  - Type-safe conversions, checked alignment routines (`align_up_checked`, `align_down`), 4-level paging decomposition (`pml4_index`, `pdpt_index`, `pd_index`, `pt_index`, `page_offset`), canonical address verification.
- **Virtual Memory Space Architecture** (`kernel/memory/memory_layout.hpp`):
  - Formally documents canonical 48-bit addressing layout with named architectural constants: `USER_SPACE_START`/`END`, `HIGHER_HALF_BASE`, `KERNEL_BASE_VIRTUAL`, `DIRECT_MAP_PHYS_LIMIT`, `KERNEL_GUARD_REGION`, `KERNEL_HEAP_START`/`END`, `VMM_TABLE_POOL_START`/`END`, and `MMIO_WINDOW_START`/`END`.
- **Physical Memory Manager (PMM)** (`kernel/memory/pmm.hpp`, `pmm.cpp`):
  - 4 KiB physical page frame bitmap allocator with fail-closed default (all frames initially reserved).
  - Normalization of Multiboot2 memory maps with strict reservation precedence (Low memory, Kernel image, Multiboot2 buffer, and PMM bitmap override Available RAM).
  - Zero-initialization of allocated physical pages via higher-half direct mapping.
  - Rejection of attempts to free reserved memory or double-free frames.
  - Safe physical memory management beyond 4 GiB without integer overflow or 32-bit truncation (verified via host arithmetic).
  - Operational cycle measurements via RDTSC.
  - Comprehensive runtime self-tests (13/13 deterministic gates passed): single-page alloc/free, multi-page contiguous alloc/free, data pattern verification, double-free detection guard, reserved memory free rejection, allocation exhaustion, fault injection, boundary testing, balance equations, and RDTSC latency benchmarking.
- **Reserved Memory Tracking & Disjoint Interval Normalization** (`kernel/memory/reserved_regions.hpp`, `reserved_regions.cpp`):
  - Unified tracker implementing interval arithmetic that subtracts all Reserved ranges from Available RAM, mathematically producing pairwise-disjoint normalized intervals with strict precedence `RESERVED > AVAILABLE`.
  - Proved that no physical address can ever be reported as both usable and reserved.
- **Virtual Memory Manager (VMM) Foundation & Dynamic Migration** (`kernel/memory/vmm.hpp`, `vmm.cpp`):
  - 4-level page table abstraction (`PageTableEntry`, `PageTable`) for PML4, PDPT, PD, and PT.
  - Dynamic page table allocation through PMM for unmapped intermediate levels.
  - Migration from bootstrap page tables to dynamic root PML4 with removal of early identity mapping.
  - Hardware-enforced Null-Page Protection (virtual 0x0 unmapped, PML4[0] not present, PML4[0..255] all unmapped).
  - Live hardware CR3 page table walking (`walk_live_cr3`) inspecting raw 64-bit PTE values.
  - Hardware W^X section permissions: `.text` RX (P=1, W=0, U=0, NX=0), `.rodata` R (P=1, W=0, U=0, NX=1), `.data` RW NX (P=1, W=1, U=0, NX=1), `.bss` & stack RW NX (P=1, W=1, U=0, NX=1).
  - `map_page`, `unmap_page`, `translate`, and `invlpg` primitives with TLB invalidation.
  - Comprehensive runtime self-tests (14/14 deterministic gates passed): live CR3 PTE reads and bit checking, W^X verification, null-page verification, lower-half unmapped verification, dynamic mapping and physical cross-verification, double-mapping detection, unmapping and TLB invalidation, multi-page mapping, non-canonical address rejection, fault injection handling, and RDTSC latency benchmarking.
- **Kernel Allocation Foundation** (`kernel/memory/kalloc.hpp`, `kalloc.cpp`):
  - Early page-granularity virtual memory allocation (`alloc_kernel_pages`, `free_kernel_pages`) and Phase 3 heap interface stubs (`kmalloc`, `kfree`).
- **Dedicated Host Unit Regression Suites** (`tests/test_pmm.cpp`, `tests/test_vmm.cpp`, `tests/test_parser.cpp`):
  - `test_pmm`: 17 tests, 369 assertions verifying PMM primitives, bitmap simulation, contiguous run allocation, 64-bit address calculations, overflow protection, double/unaligned free rejection, and the 6 memory normalization gates (unsorted, overlapping, contained, surrounding, sub-page, boundary conditions).
  - `test_vmm`: 10 tests, 611 assertions verifying canonical bounds, 4-level decomposition, bitmasks, PTE encoding, 4096-byte alignment, mapping collisions, repeated map/unmap, NX/User conversion, W^X constraints, 2MB huge page collision detection, and address rejection.
  - `test_parser`: 27 tests verifying Multiboot2 parsing, tag validation, ACPI, EFI, and command-line tokenization.
  - Total host unit verification: 54 tests, 980+ assertions verified.
- **Automated BIOS & UEFI Verification**:
  - Validated boot and autonomous debug-exit in QEMU under both legacy BIOS and OVMF UEFI firmware with code 33.


### Phase 3 — CPU and Interrupt Subsystem (COMPLETED & AUDITED)
- **Permanent 64-bit Global Descriptor Table (GDT)** with Kernel Code (0x08), Kernel Data (0x10), and Task State Segment (TSS, 0x18), verified via `sgdt`, `str`, segment registers, and VMM page mapping.
- **Task State Segment (TSS) & Dedicated IST Infrastructure**: 104-byte TSS with dedicated, PMM-allocated, VMM-mapped RW NX stacks: IST1 (#DF, 16 KiB), IST2 (#PF, 16 KiB), and IST3 (#MC/Critical, 16 KiB), bounded by non-present guard pages.
- **Interrupt Descriptor Table (IDT)**: 256 gates loaded via `lidt`, verified via `sidt`, with 24 active gates wired to low-level assembly stubs and dedicated IST assignments (#DF->IST1, #PF->IST2, #NMI/#MC->IST3).
- **Normalized Exception Assembly Stubs**: 176-byte `InterruptFrame` preserving 15 GPRs, differentiating error-code vectors from non-error-code vectors, System V AMD64 ABI compliant.
- **Central Exception Dispatcher & Runtime Hardware Proofs**:
  - Breakpoint (`#BP` / INT3, Vector 3): Verified during boot with safe return and hit counter.
  - Page Fault (`#PF`, Vector 14): Verified in isolated QEMU test with CR2 capture, full architectural error code decoding, and IST2 stack confirmation.
  - Double Fault (`#DF`, Vector 8): Hardware double-fault verified on IST1 stack without triple faulting.
  - General Protection Fault (`#GP`, Vector 13): Verified with raw error code `0x0028` and decoded GDT selector index 5.
- **Programmable Interrupt Controller (Dual 8259 PIC)**: Remapped to vectors `0x20..0x2F`, initial fail-closed masking, spurious IRQ7/15 detection, and EOI signaling.
- **APIC Hardware Reconnaissance**: CPUID and `IA32_APIC_BASE` MSR (0x1B) interrogation (APIC detection confirmed; APIC timer initialization deferred).
- **Timer Interrupt Foundation (PIT 8254)**: Configured in Mode 2 (Rate Generator, command byte `0x34`) at 100 Hz (divisor 11932), IRQ0 unmasked, periodic tick counter incremented via hardware interrupts with PIC EOI.
- **Interrupt Control API**: Real CPU `sti`, `cli`, `save_and_disable_interrupts`, and `restore_interrupt_state` confirmed via RFLAGS.IF tracking.


### Phase 4 — Device and Hardware Abstraction (COMPLETED, AUDITED & FROZEN)
- **PS/2 8042 Controller Foundation**: Port 0x60 (Data) & 0x64 (Status/Command) management, bounded wait primitives, buffer flushing, configuration byte setup (IRQ1 enabled, translation enabled), controller self-test (0xAA/0x55), and dual-channel detection.
- **PS/2 Keyboard Driver & Interrupt Pipeline**: Interrupt-driven keyboard input via IRQ1 (Vector 33 / 0x21 on Master PIC), minimal ISR discipline (zero heap allocation, fast EOI signaling, event queue feeding).
- **Scan Code Set 1 Decoder**: Host-testable state machine converting raw scancode streams into structured `KeyEvent` objects, handling make/break codes, extended prefixes (`0xE0`), multi-byte sequences (`0xE1`), and modifier tracking (Shift, Ctrl, Alt, CapsLock toggle).
- **Bounded Input Event Queue**: Strictly Single-Producer Single-Consumer (SPSC) lock-free circular ring buffer (`InputEventQueue<128>`) with monotonic head/tail indices, compiler memory barriers, elimination of shared counter race conditions, deterministic full-capacity handling, and dropped event telemetry.
- **PCI Configuration Space Bus Enumeration**: Standard I/O port 0xCF8/0xCFC access, full generic 256-bus scan (`0..255`), fast slot-skip on empty slots (`0xFFFF`/`0x0000`), multi-function device discovery, read-only Base Address Register (BAR) decoding (I/O, 32-bit Memory, synthetic 64-bit Memory with prefetchable flags; live QEMU 64-bit BAR verified not present on default topology), and PCI class formatting.
- **Linear Framebuffer Subsystem**: Multiboot2 framebuffer geometry validation, overflow rejection, unaligned physical base address page-alignment normalization, dynamic higher-half MMIO window mapping via VMM with W^X NX=1 supervisor protections, strict elimination of silent truncation via `calculate_mapping_plan()` and 512 MiB MMIO capacity validation, checked coordinate bounds arithmetic (`is_pixel_in_bounds`), pixel format detection (RGB/BGR/ARGB/RGBA/BGRA), and overflow-safe 2D drawing primitives (`put_pixel`, `fill_rect`, `clear`).
- **Unified Console Abstraction**: Multiplexed output routing across Serial (`COM1`, `0xE9`) and VGA Text Buffer (`0xB8000`), with cursor and color attributes.
- **Device Registry & Hardware Discovery Report**: Subsystem device registration (`DeviceInfo`, `DeviceRegistry`), boot-time hardware inventory logging (`HardwareReport`), and interactive keyboard echo loop.
- **Comprehensive Verification**: 5 host unit test suites (8,810 assertions in `test_phase4`), live QEMU BIOS & UEFI verification, isolated hardware tests (`test-pci`, `test-keyboard`, `test-framebuffer`), and live interactive keyboard input verification via QEMU monitor (`test-interactive`). All Phase 1–3 invariants preserved. Phase 4 is officially frozen.

### Phase 5 — Process and Threading Subsystem (COMPLETED, AUDITED & FROZEN)
- **Ring 0 Kernel Thread Model**: Strictly supervisor-mode threading model (`CS=0x08`, `SS=0x10`); zero Ring 3/userland transitions, zero syscall/sysret instructions.
- **Thread States & Lifecycle**: Strongly typed `ThreadState` state machine (`Created`, `Ready`, `Running`, `Blocked`, `Terminated`, `Idle`), unique thread IDs (`tid=1` bootstrap, `tid=2` idle, `tid>=3` workers).
- **Assembly Context Switching (`context_switch.asm`)**: SysV AMD64 callee-saved register preservation (`RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP`, `RIP`, `RFLAGS`), 80-byte 16-byte aligned `ThreadContext`, and `thread_bootstrap_trampoline` with `(RSP + 8) % 16 == 0` SysV ABI alignment on function entry.
- **Hardware-Enforced Stack Isolation**: 16 KiB PMM-backed usable stacks mapped `RW NX` in dynamic 4 KiB VMM window (`0xFFFFFFFF72000000ULL`), bounded by 4 KiB non-present guard pages to catch stack overflows via `#PF` on IST2.
- **Bounded Circular Ready Queue**: 64-element FIFO queue with $O(1)$ operations, duplicate prevention, and linear compaction.
- **Round-Robin Scheduler**: Cooperative `yield()`, priority tracking, reentrancy guards, and non-blocking state queries.
- **Preemptive Multitasking via PIT IRQ0**: Hooked into 100 Hz timer tick (`Timer::set_tick_hook`), time-slice accounting (20 ms timeslice, `DEFAULT_TIMESLICE_TICKS = 2`), transparent preemption and return via `iretq`.
- **Idle Thread & CPU Halt Loop**: Dedicated `tid=2` idle thread executing `while(true) halt();`, scheduled exclusively when the ready queue is exhausted.
- **Deferred Stack Reclamation**: Terminated thread stacks are safely deferred and reclaimed via `reclaim_deferred_stacks()` pool scan (unmapped in VMM, freed to PMM) by succeeding threads.
- **Comprehensive Verification**: 7 host unit test suites (524 assertions), 7 live QEMU automated test modes (`test-scheduler`, `test-preemption`, `test-context`, `test-stack`, `test-thread-exit`, `test-thread-stress`, `test-phase5-live`), BIOS and UEFI boot regression tests. Phase 5 is officially frozen.


### Phase 6 — System Call Interface (COMPLETED, AUDITED & FROZEN)
- `SYSCALL` / `SYSRET` 64-bit fast system call mechanism configured via AMD64 MSRs:
  - `IA32_EFER` (`0xC0000080`): Bit 0 `SCE` enabled.
  - `IA32_STAR` (`0xC0000081`): Kernel CS/SS base `0x0008`, User selector base `0x0020` (`0x0020000800000000ULL`).
  - `IA32_LSTAR` (`0xC0000082`): Target RIP `syscall_entry` in higher-half kernel space (`0xFFFFFFFF80108210`).
  - `IA32_SFMASK` (`0xC0000084`): `0x0000000000044700ULL` (masks IF, DF, TF, NT, AC).
- System V AMD64 Syscall ABI:
  - Syscall Number in `RAX`, Arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`. Return value in `RAX`.
  - Callee-saved registers (`RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP`), caller `RIP` (`RCX`), and caller `RFLAGS` (`R11`) preserved.
- Low-Level Assembly Entry Path (`syscall_entry.asm`):
  - 128-byte 16-byte aligned `SyscallFrame` with exact byte offsets.
  - Ring 0 vs Ring 3 caller differentiation via caller RIP sign bit in `RCX`. Ring 3 switches to `TSS.RSP0` (`0xFFFFFFFF70010004`).
  - Architectural return via `sysretq` (Userland) or `push r11; popfq; jmp rcx` (Kernel).
- Syscall Dispatcher & Core System Calls (`syscall.cpp`):
  - `SYS_write_debug` (1), `SYS_yield` (2), `SYS_getpid` (3), `SYS_get_ticks` (4).
  - Standard error codes: `SYS_SUCCESS` (0), `SYS_ERR_NOSYS` (-1), `SYS_ERR_INVAL` (-2), `SYS_ERR_FAULT` (-3), `SYS_ERR_PERM` (-4), `SYS_ERR_NOMEM` (-5).
- Comprehensive Verification:
  - 5 host unit test suites (66 assertions in `test_phase6.cpp`).
  - Cumulative host tests: 10,444 pure assertions + 27 parser test cases = 10,471 host checks.
  - 4 live QEMU automated tests (`scripts/test_phase6.py`): `test-syscall-init`, `test-syscall-dispatch`, `test-syscall-yield`, `test-phase6-live` all passing with exit code 33.
  - Phase 6 is officially frozen. Zero Phase 7 leakage.

### Phase 7 — Minimal Userland (COMPLETED, AUDITED & FROZEN)
- Architectural Ring 3 Hardware Transition:
  - Low-level `iretq` entry assembly (`enter_ring3`) establishing `CS=0x33`, `SS=0x2B`, canonical `RIP`/`RSP`, and `RFLAGS=0x202` (IF=1).
  - Complete general-purpose register sanitization (`xor` of all 15 GPRs) prior to userland execution to prevent kernel information leakage.
- GDT User Segments & Dynamic Table Expansion:
  - Added slot 5 (`0x28` UserData / RPL 3 `0x002B`) and slot 6 (`0x30` UserCode / RPL 3 `0x0033`).
  - Dynamic limit expansion via `PermanentGdt::install_user_descriptors()` upon process creation, preserving limit 39 for Phase 3 fault tests.
- User Address Space & Stack Architecture:
  - Canonical 48-bit lower-half addressing (`0x1000 .. 0x00007FFFFFFFFFFF`) with 4 KiB null-page guard.
  - User stack: 16 KiB (4 pages, `0x00007FFFFFFFA000 .. 0x00007FFFFFFFDFFF`) with 16-byte alignment (`0x00007FFFFFFFE000`) and unmapped 4 KiB guard page (`0x00007FFFFFFF9000`).
  - W^X and NX permissions: User stack mapped with `NoExecute`.
- Freestanding Minimal ELF64 Loader (`elf_loader.cpp`):
  - Validates ELF64 headers, machine architecture (`EM_X86_64`), executable type (`ET_EXEC`).
  - Strict segment bounds checking, integer overflow defense (`p_memsz >= p_filesz`), kernel/null address overlap rejection.
  - Page-granularity `PT_LOAD` mapping with exact W^X permissions and BSS zeroing.
- User Memory Validation & Protection:
  - `UserMemoryValidator` verifying range math, canonical limits, and 4-level page table permissions via `g_vmm.translate`.
- System Call ABI Roundtrip:
  - Syscall number `SysExit = 5` added.
  - Ring 3 user buffer validation in `SysWriteDebug`.
  - Process-aware `SysGetPid` returning user PID.
  - Returning via `o64 sysret` (`SYSRETQ`) to CPL=3.
- User Process Management & Exception Isolation:
  - `ProcessManager` and `Process` structures with backing kernel threads and lifecycle states.
  - Containment of userland `#PF` and `#GP` exceptions without kernel panic.
- Minimal Freestanding Userland Init Program (`user/init.cpp`):
  - Ring 3 program emitting `[USER_R3_ENTERED]`, `[USER_SYSCALL_OK]`, `[USER_GETPID_OK]`, `[USER_TICKS_OK]`, `[USER_PREEMPT_OK]`, `[USER_YIELD_OK]`, `[USER_EXIT_OK]`.
- Deterministic Verification:
  - 87 host unit assertions across 6 gates (`tests/test_phase7.cpp`).
  - Cumulative host tests: 10,531 pure assertions + 27 parser test cases = 10,558 host checks across 8 suites.
  - 7 live QEMU automated test modes (`scripts/test_phase7.py`) all passing with exit code 33.
  - Zero regression across all Phase 1–6 tests (BIOS, UEFI, Faults, Phase 4, Phase 5, Phase 6, CTest).

### Phase 8 — Filesystem
- Virtual Filesystem (VFS) abstraction: `vnode`, `file`, `filesystem_type`, `mount_point`.
- RAM-based initrd filesystem (TarFS / CPIO / RamFS) for early userland binaries and configs.
- Block device driver interface (ATA / AHCI / NVMe / VirtIO-Block).
- Initial disk filesystem driver (Ext2 or Fat32) with read, write, directory traversal, and metadata operations.

### Phase 9 — Symmetric Multiprocessing (SMP)
- ACPI MADT (Multiple APIC Description Table) parsing for CPU core enumeration.
- Application Processor (AP) startup via INIT-SIPI-SIPI IPI sequence.
- Per-CPU data structures (`gs` register base via `IA32_GS_BASE` / `swapgs`).
- Kernel concurrency primitives: spinlocks, tickets, reader-writer locks, atomic operations.
- Per-CPU scheduler run queues and work-stealing.

### Phase 10 — Networking Stack
- Network interface card (NIC) drivers: Intel 82540EM (e1000) and VirtIO-Net.
- Layer 2: Ethernet frame parsing, MAC addressing.
- Layer 3: ARP (Address Resolution Protocol), IPv4 packet handling, ICMP (Ping).
- Layer 4: UDP datagrams, TCP connection state machine.
- BSD-style socket API for userland networking.

### Phase 11 — Security Architecture
- Privilege separation: Kernel (Ring 0), Userland (Ring 3).
- Memory protection: Supervisor-only bit (`U/S`), No-Execute (`NX`), Supervisor Mode Execution Prevention (`SMEP`), Supervisor Mode Access Prevention (`SMAP`).
- Process capability and permission model.
- Resource limits (memory, open file descriptors, CPU time).
- Cryptographic pseudorandom number generator (CSPRNG) via hardware `RDRAND`/`RDSEED`.
- Security audit event logging.

### Phase 12 — Storage and Package Management
- Persistent partition table parsing (GPT - GUID Partition Table).
- Package format and archive structure (`.lpkg`).
- Integrity validation, metadata parsing, dependency resolution, rollback support.

### Phase 13 — Graphical Subsystem
- Desktop display compositor utilizing linear framebuffer.
- Font engine supporting TrueType or bitmap scalable fonts.
- Window management primitives, event dispatching (mouse, keyboard, redraw).
- Native GUI toolkit and desktop terminal emulator.
