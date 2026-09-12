# LlamaOS/A Debugging Guide

This guide describes the procedures and tools for debugging LlamaOS/A across emulation, GDB symbol analysis, register dumps, and serial logging.

---

## 1. Serial and Debug Port Monitoring

LlamaOS/A routes console output simultaneously across three primary channels:
1. **16550 UART COM1 (`0x3F8`)**: Primary serial channel connected to QEMU's `-serial stdio` or redirected to file/pty.
2. **Hypervisor Debug Port (`0xE9`)**: Bochs/QEMU direct debug port. All characters written to port `0xE9` are captured immediately by hypervisor logging without requiring UART baud configuration or handshaking.
3. **80x25 VGA Text Mode (`0xB8000`)**: Video console displayed on screen.

### QEMU Execution with Serial Logs
```bash
qemu-system-x86_64 -m 512M -cdrom build/llamaos.iso -serial file:serial.log -display none
```

---

## 2. GDB Remote Debugging

The build system includes DWARF debug symbols in `build/llamaos.elf` (`-g -F dwarf`).

### Step 1: Start QEMU with GDB Server
```bash
make qemu-gdb
# Equivalent command:
# qemu-system-x86_64 -m 512M -cdrom build/llamaos.iso -serial stdio -s -S
```
This pauses the virtual CPU at initial reset before executing any instructions, listening on TCP port `1234`.

### Step 2: Attach GDB
In a separate terminal:
```bash
gdb build/llamaos.elf
```

Inside the GDB session:
```gdb
# Connect to QEMU remote target
(gdb) target remote :1234

# Set breakpoint at 32-bit assembly entry point
(gdb) break _start

# Set breakpoint at C++ higher-half entry point
(gdb) break kernel_main

# Set breakpoint at panic handler
(gdb) break llamaos::panic_handler

# Continue execution until breakpoint
(gdb) continue

# Inspect registers in 64-bit mode
(gdb) info registers
(gdb) print/x $cr0
(gdb) print/x $cr3
(gdb) print/x $cr4

# Disassemble current instruction stream
(gdb) x/10i $rip

# Stack backtrace
(gdb) backtrace
```

---

## 3. QEMU Hardware and Interrupt Tracing

When diagnosing boot triple faults, invalid page mappings, or unexpected interrupts:
```bash
qemu-system-x86_64 \
    -m 512M \
    -cdrom build/llamaos.iso \
    -serial stdio \
    -no-reboot \
    -d int,cpu_reset,guest_errors \
    -D qemu.log
```

- `-d int`: Logs all CPU interrupts, traps, and exceptions with vector numbers and error codes.
- `-d cpu_reset`: Logs complete architectural register dumps upon CPU reset or triple fault.
- `-d guest_errors`: Reports invalid guest register accesses or unmapped I/O port writes.
- `-no-reboot`: Suspends QEMU execution upon triple fault instead of resetting in a loop.

---

## 4. Architectural Panic Diagnostics

When an unrecoverable condition occurs, `KPANIC(message)` or `KASSERT(condition)` invokes `panic_handler`:
1. Disables all interrupts (`cli`).
2. Displays a red visual alert banner on VGA console and writes diagnostic output to COM1 and port `0xE9`.
3. Dumps architectural control registers:
   - **`CR0`**: Protected mode, paging, FPU emulation flags.
   - **`CR2`**: Linear fault address during a `#PF` (Page Fault Exception).
   - **`CR3`**: Physical base address of the PML4 root page directory table.
   - **`CR4`**: PAE, OSFXSR, OSXMMEXCPT, SMEP, SMAP feature enable flags.
   - **`RFLAGS`**: CPU condition codes and interrupt enable bit (`IF`).
   - **`IA32_EFER`**: Long Mode Active (`LMA`), Long Mode Enable (`LME`), No-Execute Enable (`NXE`).
4. Signals debug exit port `0xF4` with failure code `0x1F` to trigger automated test failure.
5. Halts the processor deterministically (`hlt` loop).
