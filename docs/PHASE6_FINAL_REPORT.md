# LlamaOS/A - Phase 6 System Call Interface Final Verification Report

**Document Version:** 1.0.0  
**Phase Status:** COMPLETED, AUDITED, VERIFIED & FROZEN  
**Target Milestone:** Phase 6 — System Call Interface  
**Architecture:** x86-64 (AMD64 Long Mode, Ring 0 Foundation)  
**Host Environment:** Linux (x86_64), GCC 13.3.0, NASM 2.16.01, GNU Make / CMake 3.28, QEMU 8.2.2  

---

## 1. Executive Summary

Phase 6 introduces the architectural 64-bit system call mechanism for LlamaOS/A. Built atop the frozen, verified foundation of Phases 1–5, Phase 6 implements the hardware `SYSCALL` / `SYSRET` interface utilizing AMD64 Model-Specific Registers (MSRs), establishes the System V AMD64 system call ABI, constructs a low-level assembly entry and return path, and implements a deterministic, type-safe kernel system call dispatcher.

In accordance with strict architectural phasing and hard scope controls:
- **Ring 0 Foundation First:** The `SYSCALL` instruction entry, context frame capture, dispatcher invocation, and return semantics are proven and verified from Ring 0 first. This prevents the classic "chicken-and-egg" kernel development failure mode where syscall dispatching bugs are conflated with Ring 3 userland page table or TSS configuration errors.
- **Zero Phase 7 Leakage:** No Ring 3 user processes, no ELF loader, no freestanding libc, no virtual filesystem (VFS), no networking, and no userland shell were introduced.
- **Minimal Core System Calls:** Exactly four core system calls are implemented: `SYS_write_debug` (1), `SYS_yield` (2), `SYS_getpid` (3), and `SYS_get_ticks` (4).
- **Comprehensive Verification:** 5 host unit test suites verifying 66 assertions (`tests/test_phase6.cpp`), 4 live QEMU automated test modes (`scripts/test_phase6.py`), and complete zero-regression verification across all prior phases (BIOS, UEFI, Faults, Phase 4, Phase 5, and CTest).

---

## 2. Architectural System Call ABI & MSR Configuration

### 2.1 CPU Hardware MSR Configuration
AMD64 Long Mode provides fast system call entry and exit via specialized MSRs configured during `SyscallManager::init()`:

1. **`IA32_EFER` (`0xC0000080`):**
   - System Call Extensions (bit 0, `EFER_SCE_BIT = 1`) enabled via `rdmsr`/`wrmsr`.
   - Without SCE set to 1, executing `syscall` generates an Invalid Opcode (`#UD`) exception.
2. **`IA32_STAR` (`0xC0000081`):**
   - Bits `[47:32]`: Kernel CS and SS selector base (`0x0008`).
     - On `syscall`, CPU loads `CS = STAR[47:32]` (`0x0008`) and `SS = STAR[47:32] + 8` (`0x0010`).
   - Bits `[63:48]`: User 32/64 selector base (`0x0020`).
     - On `sysretq`, CPU loads `SS = STAR[63:48] + 8` (`0x002B`, User Data) and `CS = STAR[63:48] + 16` (`0x0033`, User Code).
   - Configured value: `0x0020000800000000ULL`.
3. **`IA32_LSTAR` (`0xC0000082`):**
   - Contains the 64-bit canonical virtual address of `syscall_entry` in higher-half kernel space (`0xFFFFFFFF80108210`).
4. **`IA32_SFMASK` (`0xC0000084`):**
   - System Call Flag Mask: Specifies which bits in `RFLAGS` are cleared when `syscall` executes.
   - Mask: `0x0000000000044700ULL`, clearing:
     - `IF` (bit 9): Disables interrupts immediately upon entry to prevent nested interrupts before kernel stack setup.
     - `DF` (bit 10): Clears direction flag (System V AMD64 ABI compliance).
     - `TF` (bit 8): Disables single-step trap flag.
     - `NT` (bit 14): Clears nested task flag.
     - `AC` (bit 18): Disables alignment checking.

### 2.2 System Call Register Conventions
LlamaOS/A strictly adheres to the standard AMD64 System V System Call Convention:

| Purpose | Architectural Register | Notes |
|---|---|---|
| **Syscall Number** | `RAX` | Syscall ID (1=write_debug, 2=yield, 3=getpid, 4=get_ticks) |
| **Return Value** | `RAX` | Negative value (`-1 .. -4095`) indicates error; non-negative indicates success |
| **Argument 0** | `RDI` | 1st parameter |
| **Argument 1** | `RSI` | 2nd parameter |
| **Argument 2** | `RDX` | 3rd parameter |
| **Argument 3** | `R10` | 4th parameter (`R10` used instead of `RCX`; CPU hardware clobbers `RCX` with caller `RIP`) |
| **Argument 4** | `R8` | 5th parameter |
| **Argument 5** | `R9` | 6th parameter |
| **Caller RIP** | `RCX` | Hardware saves caller instruction pointer into `RCX` |
| **Caller RFLAGS** | `R11` | Hardware saves caller execution flags into `R11` |
| **Callee-Saved** | `RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP` | Preserved across system calls by `SyscallFrame` save/restore |

---

## 3. Low-Level Assembly Entry & Return Path (`syscall_entry.asm`)

### 3.1 `SyscallFrame` Layout & Offsets
The assembly routine pushes all architectural caller state onto the stack into a 128-byte, 16-byte aligned frame matching `SyscallFrame`:

```
Offset 0x78: Saved Caller RSP
Offset 0x70: Saved Caller RFLAGS (from R11)
Offset 0x68: Saved Caller RIP    (from RCX)
Offset 0x60: Saved RAX (Syscall Number / Return Value)
Offset 0x58: Saved RDI (Arg 0)
Offset 0x50: Saved RSI (Arg 1)
Offset 0x48: Saved RDX (Arg 2)
Offset 0x40: Saved R10 (Arg 3)
Offset 0x38: Saved R8  (Arg 4)
Offset 0x30: Saved R9  (Arg 5)
Offset 0x28: Saved RBX (Callee-saved)
Offset 0x20: Saved RBP (Callee-saved)
Offset 0x18: Saved R12 (Callee-saved)
Offset 0x10: Saved R13 (Callee-saved)
Offset 0x08: Saved R14 (Callee-saved)
Offset 0x00: Saved R15 (Callee-saved)
Total Size: 128 bytes (16 qwords, alignof = 16)
```

Both host unit tests and kernel compilation verify the exact layout via static assertions:
```cpp
static_assert(sizeof(SyscallFrame) == 128, "SyscallFrame must be 128 bytes");
static_assert(alignof(SyscallFrame) == 16, "SyscallFrame must be 16-byte aligned");
```

### 3.2 Stack Switching & Privilege-Aware Entry
Upon hardware entry:
1. Caller RSP is temporarily saved to scratch storage (`g_syscall_scratch_rsp`).
2. The caller's privilege level is determined by inspecting the caller RIP saved in `RCX`:
   - Higher-half virtual address (bit 63 set, negative): Caller originated from **Ring 0 (kernel)**. RSP is already a valid kernel stack and is restored from scratch storage.
   - Lower-half virtual address (bit 63 clear, positive): Caller originated from **Ring 3 (userland)**. The stack is switched to the active thread's kernel stack via `TSS.RSP0` (`0xFFFFFFFF70010004`).
3. Direction flag is cleared (`cld`).
4. Syscall frame pointer (`RSP`) is passed in `RDI` to `syscall_dispatch(SyscallFrame* frame)`.
5. Upon return from dispatcher, `RAX` is stored into the frame's `RAX` slot (`[rsp + 0x60]`).
6. Architectural registers are restored from the frame.
7. Return path:
   - For Ring 3 caller: `o64 sysret` (`sysretq`) restores `RIP <- RCX`, `RFLAGS <- R11`, `CS <- 0x33`, `SS <- 0x2B`.
   - For Ring 0 caller: `push r11; popfq; jmp rcx` safely restores caller flags (including interrupts) and jumps directly back to the caller instruction.

---

## 4. System Call Dispatcher & Core System Calls

The central dispatcher is implemented in `kernel/syscall/syscall.cpp`:

```cpp
int64_t SyscallManager::dispatch(SyscallFrame* frame);
```

### 4.1 Implemented System Calls

| Syscall Number | Name | Arguments | Return Value | Description |
|---|---|---|---|---|
| `1` | `SYS_write_debug` | `arg0`: pointer `buf`<br>`arg1`: `len` | Number of bytes written (or `-EFAULT`, `-EINVAL`) | Direct output to COM1 serial console. Validates null pointer and max length (1024). |
| `2` | `SYS_yield` | None | `0` (`SYS_SUCCESS`) | Releases CPU cooperatively via Phase 5 `Scheduler::yield()`. |
| `3` | `SYS_getpid` | None | Current Thread ID ($\ge 1$) | Queries active thread ID via `Scheduler::current_thread()->id`. |
| `4` | `SYS_get_ticks` | None | Current Tick Count | Queries system timer ticks via `Timer::ticks()`. |

### 4.2 Standard Error Codes

| Error Constant | Value | Description |
|---|---|---|
| `SYS_SUCCESS` | `0` | Successful execution |
| `SYS_ERR_NOSYS` | `-1` | Invalid / unsupported system call number |
| `SYS_ERR_INVAL` | `-2` | Invalid argument (e.g. `len == 0` or `len > 1024`) |
| `SYS_ERR_FAULT` | `-3` | Invalid / bad memory address (e.g. `buf == nullptr`) |
| `SYS_ERR_PERM` | `-4` | Permission denied |
| `SYS_ERR_NOMEM` | `-5` | Out of memory |

---

## 5. Verification & Test Evidence

### 5.1 Host Unit Test Suite (`tests/test_phase6.cpp`)
All 66 assertions verified across 5 distinct test gates:
- **Gate 1: SyscallFrame Layout & Alignment (18 assertions):** Frame size (128 bytes), 16-byte alignment, exact byte offsets for all 16 register fields.
- **Gate 2: Syscall Numbering & Error Codes (11 assertions):** Syscall IDs 1..4, error code negative boundaries, error mapping.
- **Gate 3: Architectural MSR Math & Selectors (12 assertions):** STAR MSR bits `[47:32] = 0x08`, `[63:48] = 0x20`, EFER.SCE bit 0, SFMASK bits for IF/DF/TF/NT/AC.
- **Gate 4: Dispatcher Routing & Boundary Conditions (13 assertions):** Routing to write_debug, yield, getpid, get_ticks; rejection of unknown syscall numbers with `-1` (`SYS_ERR_NOSYS`); validation of `nullptr` and oversized buffers with `-3` and `-2`.
- **Gate 5: Register State Invariants & Frame Preservation (12 assertions):** Verification that callee-saved registers and caller RIP/RFLAGS slots are completely isolated and untouched by dispatcher execution.

### 5.2 Cumulative Host Verification Totals
With Phase 6 integrated, the complete host verification inventory is:

| Test Suite | File | Checks / Assertions | Status |
|---|---|---|---|
| Multiboot2 Parser | `tests/test_parser.cpp` | **27 test cases** | **PASSED** |
| Physical Memory Manager (PMM) | `tests/test_pmm.cpp` | **369 assertions** | **PASSED** |
| Virtual Memory Manager (VMM) | `tests/test_vmm.cpp` | **611 assertions** | **PASSED** |
| Descriptors & IDT | `tests/test_descriptors.cpp` | **64 assertions** | **PASSED** |
| Phase 4 Devices & Hardware | `tests/test_phase4.cpp` | **8,810 assertions** | **PASSED** |
| Phase 5 Process & Threading | `tests/test_phase5.cpp` | **524 assertions** | **PASSED** |
| Phase 6 System Call Interface | `tests/test_phase6.cpp` | **66 assertions** | **PASSED** |
| **Grand Total Pure Assertions** | | **10,444 assertions** | **PASSED** |
| **Grand Total Host Checks** (incl. Parser) | | **10,471 checks** | **PASSED** |

### 5.3 Live QEMU Hardware Test Modes (`scripts/test_phase6.py`)
Executed in QEMU 8.2.2 with hardware verification tokens, negative token rejection, and clean exit codes (`exit_code = 33`):

1. **`test-syscall-init`:**
   - Verifies real CPU reads of `IA32_EFER`, `IA32_STAR`, `IA32_LSTAR`, and `IA32_SFMASK`.
   - Result: `[PASS] (exit_code=33, elapsed=1.31s)`
2. **`test-syscall-dispatch`:**
   - Executes real CPU `syscall` instructions from assembly.
   - Tests `SYS_getpid` (returns PID 1), `SYS_write_debug` (emits `"SYSCALL_DISPATCH_OK\n"` to COM1 and Bochs port 0xE9), `SYS_get_ticks` (returns positive tick counter), and unknown syscall (returns `-1`).
   - Result: `[PASS] (exit_code=33, elapsed=1.32s)`
3. **`test-syscall-yield`:**
   - Spawns two concurrent worker threads invoking `SYS_yield` via real CPU `syscall`.
   - Verifies round-robin context switching and scheduler integration.
   - Result: `[PASS] (exit_code=33, elapsed=1.20s)`
4. **`test-phase6-live`:**
   - Comprehensive end-to-end execution of all Phase 6 capabilities.
   - Result: `[PASS] (exit_code=33, elapsed=1.28s)`

### 5.4 Full System Regression Matrix
All previous subsystem suites were re-run and confirmed with zero regressions:
- `make test-unit`: All 7 unit test binaries passed (10,471 host checks).
- `make test-bios`: Verified full boot through Milestone 6 under BIOS.
- `make test-uefi`: Verified full boot through Milestone 6 under OVMF UEFI.
- `make test-faults`: Isolated hardware fault tests (#PF, #GP, #DF) verified on dedicated IST stacks.
- `make test-phase4-live`: PCI, PS/2 keyboard, framebuffer live tests passed.
- `make test-phase5-live`: 7/7 live threading tests passed (cooperative context switch, preemption, stack guard pages, thread exit, 8-thread stress).
- `ctest --test-dir build-cmake`: 7/7 CTest test suites passed in CMake.

---

## 6. Phase Freeze Verdict

Phase 6 meets all architectural criteria specified in the implementation directive:
1. Strict supervisor Ring 0 foundation established before userland.
2. Stable System V AMD64 syscall ABI.
3. Fully verified MSR configuration (`EFER.SCE`, `STAR`, `LSTAR`, `SFMASK`).
4. Low-level assembly entry and clean return path.
5. Deterministic argument parsing and static switch dispatcher.
6. 100% clean builds with zero warnings (`-Wall -Wextra -Werror`).
7. 10,471 host verification checks passing.
8. All QEMU live test modes passing with positive and negative validation.

Phase 6 is hereby marked:
**PHASE 6 VERIFIED & FROZEN**
