# LlamaOS/A Development Roadmap

This document establishes the strategic 13-phase development roadmap for LlamaOS/A. Every milestone builds upon previously verified architectural components.

---

## Phase Status Summary

| Phase | Subsystem | Status | Verification Mechanism |
|---|---|---|---|
| **Phase 0** | Project Reconnaissance | **Completed** | Host environment & toolchain detection |
| **Phase 1** | Bootable Foundation | **Completed** | Hybrid ISO, Higher-Half Long Mode, Banner in QEMU |
| **Phase 2** | CPU and Interrupt Subsystem | *Planned (Next)* | IDT, GDT, Exceptions, PIC/APIC, Timer |
| **Phase 3** | Memory Management | *Planned* | PMM (Bitmap), VMM (4-Level), Kernel Heap (Slab/Buddy) |
| **Phase 4** | Device & Hardware Abstraction | *Planned* | PS/2 Keyboard, PCI Bus Enumeration, Framebuffer Driver |
| **Phase 5** | Process & Threading Subsystem | *Planned* | Kernel Threads, Preemptive Scheduler, Context Switching |
| **Phase 6** | System Call Interface | *Planned* | `SYSCALL`/`SYSRET` ABI, Argument & Boundary Validation |
| **Phase 7** | Minimal Userland | *Planned* | Init Process, Freestanding Libc, Interactive Shell |
| **Phase 8** | Virtual Filesystem (VFS) | *Planned* | VFS Node Tree, Mount Table, Initial Filesystem (TarFS/Ext2) |
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

### Phase 2 — CPU and Interrupt Subsystem (NEXT MILESTONE)
- Permanent 64-bit Global Descriptor Table (GDT) with Kernel Code, Kernel Data, User Code, User Data, and Task State Segment (TSS).
- Interrupt Descriptor Table (IDT) with 256 gates.
- Exception handlers for all 32 x86-64 CPU exceptions (Divide Error `#DE`, Double Fault `#DF`, General Protection `#GP`, Page Fault `#PF`, etc.) capturing register state, CR2, and error codes.
- Programmable Interrupt Controller (8259 PIC) remapping to vectors `0x20..0x2F` and masking.
- Local APIC (Advanced Programmable Interrupt Controller) detection and initialization.
- Programmable Interval Timer (PIT 8253/8254) and APIC Timer calibration for millisecond-precision system ticks.
- Interrupt-driven serial input and PS/2 keyboard controller driver.

### Phase 3 — Memory Management
- **Physical Memory Manager (PMM)**: Bitmap-based page frame allocator managing available physical RAM regions reported by Multiboot2.
- **Virtual Memory Manager (VMM)**: Dynamic 4-level page table management (`PML4`, `PDPT`, `PD`, `PT`), page mapping/unmapping primitives, page permission flags (Present, Writable, User, NX, Cache-Disable).
- **Higher-Half Unmapping**: Unmap identity PML4[0] to enforce null pointer protection.
- **Kernel Heap Allocator**: Slab allocator / Buddy allocator for dynamic kernel allocations (`kmalloc`, `kfree`).
- Allocation diagnostics, guard pages, and out-of-memory handling.

### Phase 4 — Device and Hardware Abstraction
- Unified Hardware Abstraction Layer (HAL) and Driver Model.
- PCI (Peripheral Component Interconnect) bus enumeration and device identification.
- PS/2 Keyboard & Mouse driver with scan code decoding.
- High Precision Event Timer (HPET) and APIC timer drivers.
- Linear Framebuffer graphics driver with double-buffering and 8x16 bitmap font renderer.

### Phase 5 — Process and Threading Subsystem
- Process Control Block (PCB) and Thread Control Block (TCB) abstractions.
- Per-thread kernel stacks and saved register contexts.
- Preemptive Round-Robin / Priority Scheduler driven by timer interrupt.
- Context switching assembly routine (`switch_context`).
- Process lifecycle: creation, execution, suspension, termination, zombie reclamation.
- User-mode transition via `iretq` or `sysretq`.

### Phase 6 — System Call Interface
- `SYSCALL` / `SYSRET` 64-bit fast system call mechanism configured via MSRs (`STAR`, `LSTAR`, `SFMASK`).
- Safe argument extraction and boundary validation between user space and kernel space.
- Standard error code conventions (`EPERM`, `ENOENT`, `ENOMEM`, `EFAULT`, `EINVAL`, etc.).
- Initial core system calls: `sys_exit`, `sys_write`, `sys_read`, `sys_mmap`, `sys_munmap`, `sys_fork`/`sys_spawn`, `sys_yield`, `sys_getpid`.

### Phase 7 — Minimal Userland
- Executable and Linkable Format (ELF64) loader for user binaries.
- Freestanding minimal userland libc (system call wrappers, string functions, basic stdio).
- `init` process: PID 1 launching user environment.
- Interactive userland shell (`llama-shell`) supporting built-in commands: `help`, `clear`, `echo`, `info`, `mem`, `ps`, `ls`, `cat`, `reboot`.

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
