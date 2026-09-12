# LlamaOS/A Changelog

All notable changes to LlamaOS/A are documented in this file.

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
