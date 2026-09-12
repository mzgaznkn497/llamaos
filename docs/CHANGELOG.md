# LlamaOS/A Changelog

All notable changes to LlamaOS/A are documented in this file.

---

## [0.1.1-audit] - 2026-09-12 — Phase 1 Hardening, Security, Correctness, and Reliability Audit

### Fixed & Hardened
- **Multiboot2 Parser Security Hardening** (`kernel/arch/x86_64/boot/multiboot2.cpp`, `multiboot2.hpp`):
  - Fixed unbounded tag iteration: added minimum size check (16 bytes), sanity ceiling (8 MiB), and pointer addition overflow checks.
  - Added tag header pre-validation: ensured tag header (`type`, `size`) is within remaining buffer before dereference.
  - Added zero-size and undersized tag rejection (`size < 8`) to eliminate infinite loops.
  - Guarded tag advance arithmetic against 32-bit addition overflow and 64-bit address wraparound.
  - Bounded string tag extraction for `Cmdline` and `BootLoaderName`: strings copied into fixed buffers with guaranteed NUL termination.
  - Memory-map security: entry size validated (`24 <= entry_size <= 1024`), incomplete trailing entry rejection, 64-bit region range overflow check (`base + length >= base`).
  - Total usable RAM accumulation protected against integer overflow with saturation arithmetic and warning flag.
  - Added detection and warning for memory-map buffer truncation (`mmap_truncated`).
  - Framebuffer validation: checked common header length (32 bytes), direct RGB sub-header length (38 bytes), dimension sanity (up to 8K), and pitch/height 64-bit multiplication overflow check.
  - ACPI RSDP verification: signature validated (`"RSD PTR "`), 20-byte checksum verified for ACPI 1.0, 36-byte checksum verified for ACPI 2.0+, OEM ID safely copied.
  - EFI 64-bit system table pointer validated and distinguished as firmware physical pointer.
- **Command-Line Parsing Correctness** (`kernel/arch/x86_64/boot/multiboot2.cpp`, `multiboot2.hpp`):
  - Replaced naive substring search (`strchr`) with exact token-based parser (`has_argument`, `is_test_mode`, `is_debug_mode`).
  - Prevented false positives from parameters containing 't' or 's' (e.g. `status`, `testingfoo`).
- **Higher-Half Runtime MMU Verification** (`kernel/kernel_main.cpp`):
  - Replaced linker symbol inspection with active runtime architectural checks: instruction pointer (`RIP`), stack pointer (`RSP` within 64 KiB kernel stack), segment registers (`CS=0x08`, `DS=0x10`), and MSR EFER flags (`LMA=1`, `NXE=1`).
  - Implemented hardware page table walk: inspecting CR3 root, PML4[511], PDPT[510], and PD[0] (verifying 2MB huge page entry `0x83`).
- **Standard Freestanding Types & ABI Conformance** (`kernel/core/types.hpp`, `string.hpp`, `string.cpp`):
  - Defined primitive types using compiler builtin macros (`__SIZE_TYPE__`, `__UINTPTR_TYPE__`, etc.) to guarantee ABI compatibility with compiler intrinsics and host unit test frameworks.
  - Standardized `strchr` signature to match C/C++ freestanding expectations.
  - Added `UINT64_MAX`, `UINT32_MAX`, and standard integer limits.
- **Enhanced Formatted Printing** (`kernel/core/kprint.cpp`):
  - Added precision specifier support (e.g. `%.6s`) for fixed-width string rendering.
  - Fixed 64-bit integer printing to eliminate silent 32-bit narrowing casts for memory capacities exceeding 4 GiB.
- **Host-Side Regression Test Suite** (`tests/test_parser.cpp`):
  - Added 14 unit and negative regression test cases verifying malformed magic, zero/unaligned pointers, undersized/oversized buffers, zero tag size, unterminated strings, corrupted mmap entry sizes, arithmetic overflows, invalid framebuffers, corrupted ACPI checksums, and token parsing.
  - Integrated into both `Makefile` (`make test`) and `CMakeLists.txt` (`test-boot`).

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
