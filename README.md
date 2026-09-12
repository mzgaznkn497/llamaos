# LlamaOS/A

```
================================================================================
  _     _                           ____   _____       _   
 | |   | |                         / __ \ / ____|     / \  
 | |   | |     __ _ _ __ ___   __ | |  | | (___      / _ \ 
 | |   | |    / _` | '_ ` _ \ / _`| |  | |\___ \    / ___ \
 | |___| |___| (_| | | | | | | (_|| |__| |____) |  / /   \ \
 |_____|______\__,_|_| |_| |_|\__,_\____/|_____/  /_/     \_\
================================================================================
```

**LlamaOS/A** is a modern, modular, freestanding 64-bit operating system built from scratch for x86-64 PCs. It boots in long mode on UEFI (primary) and BIOS platforms via the Multiboot2 specification, featuring a higher-half kernel architecture, C++20 freestanding core, hardware abstraction layer, memory safety controls, and continuous automated QEMU test validation.

---

## Current Status: Phase 1 — Bootable Foundation (Completed)

- **Target Architecture**: x86-64 (AMD64 / Intel 64) Long Mode
- **Boot Environment**: Hybrid UEFI (OVMF) and legacy BIOS via Multiboot2
- **Address Space**: Higher-half kernel linked at `-2 GiB` (`0xFFFFFFFF80000000`)
- **Paging Model**: 4-Level Paging with PAE and No-Execute (NX) bit enforcement
- **Language**: Freestanding C++20 with NASM assembly bootstrap
- **Toolchain**: GCC 13.3+ / Clang, NASM 2.16+, GNU ld / LLD, CMake 3.20+ / Make
- **Testing**: Automated end-to-end boot tests in QEMU with ISA debug exit

---

## Quick Start

### Prerequisites (Ubuntu / Debian)
```bash
sudo apt-get update
sudo apt-get install -y build-essential nasm qemu-system-x86 xorriso mtools grub-pc-bin grub-efi-amd64-bin ovmf
```

### Build the Kernel and Bootable ISO
```bash
make
```

### Run Automated Boot Tests
```bash
# Test BIOS boot flow
make test

# Test UEFI boot flow
python3 scripts/test_boot.py build/llamaos.iso --uefi
```

### Launch Interactive QEMU
```bash
# BIOS boot
make qemu

# UEFI boot (via OVMF)
make qemu-uefi

# GDB remote debugging (port :1234)
make qemu-gdb
```

---

## Directory Structure

```
.
├── CMakeLists.txt                # CMake configuration
├── Makefile                      # Top-level GNU Makefile
├── README.md                     # Project overview and quickstart
├── boot/
│   └── grub/
│       └── grub.cfg              # GRUB Multiboot2 bootloader configuration
├── docs/
│   ├── ARCHITECTURE.md           # System design, virtual memory, and layers
│   ├── BUILDING.md               # Build instructions and cross-compilation
│   ├── CHANGELOG.md              # Milestone changelog
│   ├── DEBUGGING.md              # GDB, QEMU tracing, and register diagnostics
│   └── ROADMAP.md                # 13-phase development roadmap
├── kernel/
│   ├── arch/
│   │   └── x86_64/
│   │       ├── boot/
│   │       │   ├── boot.asm      # 32-to-64 bit bootstrap, page tables, GDT
│   │       │   ├── multiboot2.cpp# Multiboot2 tag stream parser
│   │       │   └── multiboot2.hpp
│   │       ├── cpu/
│   │       │   ├── cpu.cpp       # CPUID feature detection & topology
│   │       │   ├── cpu.hpp
│   │       │   ├── io.hpp        # Port I/O primitives
│   │       │   └── msr.hpp       # Model-Specific Register read/write
│   │       └── linker.ld         # Higher-half linker script (-2 GiB)
│   ├── core/
│   │   ├── kprint.cpp            # Formatted printing and log levels
│   │   ├── kprint.hpp
│   │   ├── panic.cpp             # Deterministic panic and register dump
│   │   ├── panic.hpp
│   │   ├── runtime.cpp           # Freestanding C++ runtime and stack canary
│   │   ├── string.cpp            # Freestanding memory and string functions
│   │   ├── string.hpp
│   │   └── types.hpp             # Fixed-width types and address conversions
│   ├── drivers/
│   │   ├── serial.cpp            # 16550 UART driver (COM1 + port 0xE9)
│   │   ├── serial.hpp
│   │   ├── vga.cpp               # 80x25 text-mode console driver
│   │   └── vga.hpp
│   └── kernel_main.cpp           # Kernel initialization and self-tests
└── scripts/
    ├── build.sh                  # Build helper script
    ├── run_qemu.sh               # QEMU launcher helper
    └── test_boot.py              # Automated test harness runner
```

---

## Documentation Links

- [Architecture Design](docs/ARCHITECTURE.md)
- [Development Roadmap](docs/ROADMAP.md)
- [Building Guide](docs/BUILDING.md)
- [Debugging Guide](docs/DEBUGGING.md)
- [Changelog](docs/CHANGELOG.md)

---

## License

LlamaOS/A is distributed under the MIT License.
