# Building LlamaOS/A

This guide details the prerequisites, toolchain requirements, build targets, and testing workflows for LlamaOS/A.

---

## 1. Toolchain Requirements

LlamaOS/A targets 64-bit x86-64 freestanding hardware. It can be compiled using modern Linux distributions or cross-compilation environments on macOS / Windows (via WSL2 or containerization).

### Required Packages (Debian / Ubuntu 22.04 / 24.04)
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    g++ \
    nasm \
    binutils \
    qemu-system-x86 \
    xorriso \
    mtools \
    grub-pc-bin \
    grub-efi-amd64-bin \
    ovmf \
    cmake \
    python3 \
    gdb
```

### Verified Tool Versions
- **GCC / G++**: 13.3.0 or later (C++20 freestanding mode supported)
- **NASM**: 2.16.01 or later
- **GNU ld**: 2.42 or later
- **QEMU**: 8.2.2 or later (`qemu-system-x86_64`)
- **GRUB**: 2.12 (`grub-mkrescue`)
- **OVMF**: 4M UEFI firmware (`/usr/share/ovmf/OVMF.fd` or `/usr/share/OVMF/OVMF_CODE_4M.fd`)

---

## 2. Compilation and Linking Flags

### Freestanding C++ Compiler Flags
- `-std=c++20`: Enforces modern C++20 language standards.
- `-ffreestanding`: Directs the compiler that standard hosted library runtime is unavailable.
- `-fno-builtin`: Prevents standard library assumption transformations.
- `-fno-exceptions`: Disables C++ exception tables and unwind metadata.
- `-fno-rtti`: Eliminates runtime type information overhead.
- `-fno-pie -fno-pic`: Required for `-mcmodel=kernel` in 64-bit mode.
- `-fno-omit-frame-pointer`: Maintains RBP frame pointers for stack traces.
- `-fstack-protector-strong`: Hardens kernel stack frames against buffer overruns.
- `-mno-red-zone`: Prevents asynchronous interrupts from corrupting stack memory below RSP.
- `-mcmodel=kernel`: Links code within virtual address space `-2 GiB` (`0xFFFFFFFF80000000`).
- `-mgeneral-regs-only`: Prevents emission of vector instructions (SSE/AVX) before FPU registers are configured.

### Linker Flags
- `-m elf_x86_64`: Links 64-bit ELF executable.
- `-nostdlib`: Omits standard C/C++ startup runtime (`crt0.o`, etc.).
- `-static`: Eliminates dynamic linker relocations.
- `-z noexecstack`: Sets ELF stack segment permissions to non-executable.
- `-z max-page-size=0x1000`: Standard 4 KiB page boundaries.
- `-T kernel/arch/x86_64/linker.ld`: Enforces custom physical load and higher-half virtual memory mapping.

---

## 3. Build Targets

### Using GNU Make
```bash
# Build kernel ELF and hybrid bootable ISO
make

# Build only the kernel ELF (build/llamaos.elf)
make kernel

# Build bootable ISO (build/llamaos.iso)
make iso

# Execute automated QEMU boot test harness
make test

# Launch QEMU with BIOS
make qemu

# Launch QEMU with UEFI firmware
make qemu-uefi

# Launch QEMU in GDB debugging mode
make qemu-gdb

# Clean build artifacts
make clean
```

### Using CMake
```bash
# Configure build directory
cmake -B build-cmake -S .

# Build kernel ELF and ISO
cmake --build build-cmake

# Run automated boot test
cmake --build build-cmake --target test-boot
```

---

## 4. Cross-Platform Development (Windows Hosts)

To build LlamaOS/A on Windows:
1. Use **Windows Subsystem for Linux (WSL2)** with Ubuntu 22.04/24.04.
2. Install the standard toolchain packages inside WSL2.
3. If running QEMU inside WSL2, enable nested virtualization or use VcXsrv/WSLg for graphical display output, or run QEMU in `-nographic` mode.
