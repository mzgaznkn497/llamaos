# Phase 3 Final Audit & Verification Report
**Project:** LlamaOS/A  
**Architecture:** x86-64 (Long Mode, Higher-Half Canonical Space)  
**Milestone:** Phase 3 — CPU Descriptors, Exceptions & Interrupt Subsystem  
**Date of Audit:** 2026-09-13  
**Status:** **PHASE 3 VERIFIED**

---

## 1. Executive Summary

This document presents the definitive architectural and runtime audit of Phase 3 for LlamaOS/A. Phase 3 establishes the CPU descriptor, exception, and interrupt foundation for the operating system.

During this audit:
1. **PIT Source Fix:** Audited `kernel/arch/x86_64/cpu/timer.cpp`. Corrected the command byte from `0x36` (Mode 3 Square Wave) to **`0x34`** (Mode 2 Rate Generator), implemented rounded divisor calculation `(1193182 + 50) / 100 = 11932` (`0x2E9C`), and verified low-byte/high-byte access to Channel 0 data port `0x40`.
2. **Exception Hardware Proofs:** Engineered and executed isolated QEMU runtime tests verifying actual hardware CPU exceptions:
   - **Page Fault (`#PF`, Vector 14):** Deliberate unmapped memory access at `0xFFFFFFFF70000000`, captured CR2, decoded raw error code `0x0`, verified execution on dedicated `IST2` stack (`0xFFFFFFFF70009EE0`), and exited deterministically via port `0xF4` (exit code 41).
   - **Double Fault (`#DF`, Vector 8):** Corrupted `TSS.ist2` to point to unmapped guard page `0xFFFFFFFF70000000`, triggered `#PF`, produced hardware Double Fault, captured context on dedicated `IST1` stack (`0xFFFFFFFF70004EE0`) without triple faulting, and exited deterministically (exit code 17).
   - **General Protection Fault (`#GP`, Vector 13):** Loaded invalid segment selector `0x0028` (GDT index 5 > limit 4) into `DS`, captured raw error code `0x0028`, decoded `EXT=0`, `TI=0 (GDT)`, `Selector Index=5`, and exited deterministically (exit code 39).
   - **Breakpoint (`#BP` / INT3, Vector 3):** Verified software breakpoint trap gate, hit counter incremented `0 -> 1`, and clean resumption.
3. **PMM/VMM Invariant Integrity:** Confirmed PMM conservation invariant `free_frames + allocated_frames == usable_frames` holds after Phase 3 TSS/IST allocations. All 13 PMM and 14 VMM runtime self-tests passed without regressions.
4. **Dual Build Parity:** GNU `Makefile` and `CMakeLists.txt` / `ctest` execute identical test suites with 100% pass rates across Unit, BIOS, UEFI, and Fault tests.

---

## 2. Scope & Architectural Boundaries

| Subsystem Component | Phase 3 Scope Inclusion | Implementation State |
|---|---|---|
| **Permanent 64-bit GDT** | Mandatory | Completed (`gdt.cpp`, `gdt_asm.asm`) |
| **Task State Segment (TSS)** | Mandatory | Completed (`tss.cpp`) |
| **Interrupt Stack Tables (IST1..3)** | Mandatory | Completed (`tss.cpp`, guard pages active) |
| **Interrupt Descriptor Table (IDT)** | Mandatory | Completed (`idt.cpp`, 256 gates) |
| **Assembly ISR Stubs & Normalization**| Mandatory | Completed (`interrupt_stubs.asm`) |
| **Central Exception Dispatcher** | Mandatory | Completed (`interrupts.cpp`) |
| **Page Fault Handling (#PF)** | Mandatory | Completed (CR2, error codes, IST2) |
| **Double Fault Handling (#DF)** | Mandatory | Completed (IST1 containment) |
| **General Protection Fault (#GP)** | Mandatory | Completed (Selector decoder) |
| **Breakpoint (#BP / INT3)** | Mandatory | Completed (Trap gate, safe return) |
| **Dual 8259 PIC Remapping** | Mandatory | Completed (Vectors `0x20..0x2F`) |
| **APIC Foundation** | Mandatory (Detection only) | Completed (CPUID & MSR 0x1B detection) |
| **Timer Foundation (PIT 8254)** | Mandatory | Completed (Mode 2 Rate Generator, IRQ0) |
| **Interrupt Control API** | Mandatory | Completed (`sti`, `cli`, save, restore) |
| **Phase 4+ Subsystems (Scheduler, User Mode, Syscalls, VFS, Heap, SMP, GUI)** | **EXCLUDED** | Strictly deferred to future milestones |

---

## 3. PIT Source Code Verification

### Source Audit Findings
Inspection of `kernel/arch/x86_64/cpu/timer.cpp` revealed that the initial implementation wrote `outb(PIT_COMMAND, 0x36);`.

#### Command Byte Bitfield Decomposition:
```
Bit:   7 6 | 5 4 | 3 2 1 | 0
0x36 = 0 0 | 1 1 | 0 1 1 | 0  => Channel 0, Lo/Hi byte, Mode 3 (Square Wave), 16-bit Binary
0x34 = 0 0 | 1 1 | 0 1 0 | 0  => Channel 0, Lo/Hi byte, Mode 2 (Rate Generator), 16-bit Binary
```

### Corrective Action Taken
The source code was updated in `timer.cpp`:
```cpp
    // Divisor calculation using rounding to nearest integer: (base + (freq / 2)) / freq
    uint32_t divisor = (PIT_BASE_FREQ_HZ + (frequency_hz / 2)) / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    // Command byte: 0x34
    //   bit 7..6 = 00  (Channel 0)
    //   bit 5..4 = 11  (Access mode: lobyte/hibyte)
    //   bit 3..1 = 010 (Operating Mode 2: Rate Generator)
    //   bit 0    = 0   (16-bit binary mode)
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0_DATA, static_cast<uint8_t>(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, static_cast<uint8_t>((divisor >> 8) & 0xFF));

    // Unmask IRQ0 on the PIC
    PicManager::unmask_irq(0);
```

### Verification Matrix
- **Configuration:** PASS (Mode 2 Rate Generator, command byte `0x34`, divisor `11932 = 0x2E9C` for 100 Hz).
- **Host Verification:** PASS (Host math verified divisor rounding: `(1193182 + 50) / 100 = 11932`).
- **Live QEMU Verification:** PASS (Serial output confirms: `PIT Timer Initialized: Target=100 Hz, Divisor=11932 (0x2e9c), Command=0x34 (Mode 2 Rate Generator), IRQ0 unmasked.`).

---

## 4. GDT Evidence

### Architectural Specification
- **Base Structure:** `PermanentGdt::s_table` (aligned to 16 bytes).
- **Entries:**
  - Entry 0 (`0x00`): Null Descriptor (`0x0000000000000000`).
  - Entry 1 (`0x08`): 64-bit Kernel Code (`0x00AF9A000000FFFF`) — DPL 0, P=1, L=1, G=1.
  - Entry 2 (`0x10`): 64-bit Kernel Data (`0x00CF93000000FFFF`) — DPL 0, P=1, W=1, G=1.
  - Entry 3 & 4 (`0x18`): 16-byte TSS Descriptor (`Low: 0x70008b0100000067, High: 0x00000000ffffffff`) — DPL 0, P=1, Type=0x9 (Available 64-bit TSS), Base=`0xFFFFFFFF70010000`, Limit=`0x0067` (103).

### Live CPU State Capture
```
[INFO]  Permanent GDT initialized and loaded successfully:
[INFO]    GDTR Limit    : 0x0027 (size: 40 bytes)
[INFO]    GDTR Base     : 0xFFFFFFFF8012E0A0
[INFO]    Kernel Code   : Selector 0x0008 (Raw: 0x00af9a000000ffff)
[INFO]    Kernel Data   : Selector 0x0010 (Raw: 0x00cf93000000ffff)
[INFO]    TSS Selector  : Selector 0x0018 (Low: 0x70008b0100000067, High: 0x00000000ffffffff)
[INFO]   [PASS] Permanent Global Descriptor Table (GDT) & Segment Reload
```

### Hardware Register Verification
- `sgdt`: GDTR Base = `0xFFFFFFFF8012E0A0`, GDTR Limit = `0x0027` (matches C++ struct address and size).
- `read_cs()`: `0x0008`.
- `read_ds()`: `0x0010`.
- `read_ss()`: `0x0010`.
- `str`: `0x0018`.
- **TSS Busy Bit:** Bit 41 verified set by the CPU in the TSS descriptor low qword after `ltr` execution (`0x70008b...` bit 41 = 1).
- **VMM Mapping:** Virtual address `0xFFFFFFFF8012E0A0` verified mapped with `Present | Writable | NoExecute` supervisor permissions.

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (Host tests verify bitfield encodings and limit reconstructions).
- **Live QEMU Verification:** PASS

---

## 5. TSS and IST Evidence

### Architectural Allocation & Mapping
Every IST stack is allocated as 4 contiguous physical page frames (16 KiB) via PMM and mapped via VMM into dedicated higher-half virtual windows, isolated by unmapped 4 KiB guard pages:

| Region | Virtual Range | Physical Address | VMM Permissions | Guarded Status |
|---|---|---|---|---|
| **Guard Page 1** | `0xFFFFFFFF70000000 - 0xFFFFFFFF70001000` | — | Non-present | Enforced unmapped |
| **IST1 (#DF Stack)** | `0xFFFFFFFF70001000 - 0xFFFFFFFF70005000` | `0x000000000013A000` | `RW NX` Supervisor | 16 KiB valid stack |
| **Guard Page 2** | `0xFFFFFFFF70005000 - 0xFFFFFFFF70006000` | — | Non-present | Enforced unmapped |
| **IST2 (#PF Stack)** | `0xFFFFFFFF70006000 - 0xFFFFFFFF7000A000` | `0x000000000013E000` | `RW NX` Supervisor | 16 KiB valid stack |
| **Guard Page 3** | `0xFFFFFFFF7000A000 - 0xFFFFFFFF7000B000` | — | Non-present | Enforced unmapped |
| **IST3 (#MC Stack)** | `0xFFFFFFFF7000B000 - 0xFFFFFFFF7000F000` | `0x0000000000142000` | `RW NX` Supervisor | 16 KiB valid stack |
| **Guard Page 4** | `0xFFFFFFFF7000F000 - 0xFFFFFFFF70010000` | — | Non-present | Enforced unmapped |
| **TSS Page** | `0xFFFFFFFF70010000 - 0xFFFFFFFF70011000` | `0x0000000000146000` | `RW NX` Supervisor | 104 bytes populated |

### Live CPU State Capture
```
[INFO]  TSS Initialized successfully:
[INFO]    TSS Structure : VA=0xFFFFFFFF70010000, PA=0x0000000000146000 (size: 104 bytes)
[INFO]    RSP0 Base     : 0xFFFFFFFF8012E000
[INFO]    IST1 (#DF)    : Top=0xFFFFFFFF70005000, Bottom=0xFFFFFFFF70001000 (16 KiB, PA=0x000000000013A000)
[INFO]    IST2 (#PF)    : Top=0xFFFFFFFF7000A000, Bottom=0xFFFFFFFF70006000 (16 KiB, PA=0x000000000013E000)
[INFO]    IST3 (Crit)   : Top=0xFFFFFFFF7000F000, Bottom=0xFFFFFFFF7000B000 (16 KiB, PA=0x0000000000142000)
[INFO]   [PASS] Task State Segment (TSS) & Dedicated IST Stacks (PMM-backed, VMM-mapped)
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (`test_descriptors` validates TSS descriptor layout and arithmetic).
- **Live QEMU Verification:** PASS (Confirmed 16-byte alignment, distinct stack pointer values, valid physical frame translation, and non-present guard page rejection).

---

## 6. IDT Evidence

### Architectural Specification
- 256-entry Interrupt Descriptor Table located at VA `0xFFFFFFFF8012F000`.
- Loaded via `lidt`. Verified via `sidt`.
- Limit: `0x0FFF` (4096 bytes - 1).
- Base: `0xFFFFFFFF8012F000`.

### Gate Wiring & Dedicated IST Assignments
- **Exceptions (Vectors 0..21):**
  - Vector 0 (`#DE`): Interrupt Gate, IST 0.
  - Vector 1 (`#DB`): Interrupt Gate, IST 0.
  - Vector 2 (`#NMI`): Interrupt Gate, IST 3.
  - Vector 3 (`#BP`): Trap Gate, IST 0.
  - Vector 4 (`#OF`): Trap Gate, IST 0.
  - Vector 8 (`#DF`): Interrupt Gate, **IST 1**.
  - Vector 13 (`#GP`): Interrupt Gate, IST 0.
  - Vector 14 (`#PF`): Interrupt Gate, **IST 2**.
  - Vector 18 (`#MC`): Interrupt Gate, **IST 3**.
  - Vectors 10..12, 16..17, 19..21: Interrupt Gates, IST 0.
- **Hardware IRQ & Spurious:**
  - Vector 32 (IRQ0 Timer): Interrupt Gate, IST 0.
  - Vector 39 (IRQ7 Master Spurious): Interrupt Gate, IST 0.
  - Vector 47 (IRQ15 Slave Spurious): Interrupt Gate, IST 0.
  - Vector 255 (Spurious APIC): Interrupt Gate, IST 0.

### Live CPU State Capture
```
[INFO]  Interrupt Descriptor Table (IDT) loaded successfully:
[INFO]    IDTR Limit    : 0x0fff (size: 4096 bytes, 256 gates)
[INFO]    IDTR Base     : 0xFFFFFFFF8012F000
[INFO]    Configured Gates: 24 active vectors (#DE..#CP, IRQ0, IRQ7/15, APIC spurious)
[INFO]   [PASS] Interrupt Descriptor Table (IDT 256 gates) & Vector Dispatcher
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (Validated gate encoding bitfields, interrupt vs trap types, and DPL levels).
- **Live QEMU Verification:** PASS (Verified active handlers reside strictly in `.text`, unused gates have P=0, and table is mapped RW NX in VMM).

---

## 7. Breakpoint (#BP / INT3) Runtime Evidence

### Trigger & Dispatch Context
- Executed via `asm volatile("int3")` during kernel boot.
- Dispatched via IDT vector 3 (Trap Gate).
- Preserves return context on existing stack.

### Live QEMU Output
```
[INFO]  Verifying CPU Exception Subsystem via software Breakpoint (#BP / INT3)...
[INFO]  DEBUG [#BP / INT3]: RIP=0xFFFFFFFF8010B18E, CS=0x0008, RFLAGS=0x0000000000000086, RSP=0xFFFFFFFF8012DF50 (Hit #1)
[INFO]   [PASS] Breakpoint (#BP / INT3) Exception Dispatch & Safe Return (RIP=0xFFFFFFFF8010B18E, Hits=1)
[INFO]   [PASS] CPU Exception Infrastructure & Breakpoint (#BP / INT3) Verification
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS
- **Live QEMU Verification:** PASS (Confirmed non-zero RIP captured, hits incremented `0 -> 1`, safe return to kernel execution path).

---

## 8. Page Fault (#PF, Vector 14) Runtime Evidence

### Test Isolation & Trigger Mechanism
Triggered in dedicated isolated test mode (`test-pf`) via boot argument `test-pf`. Memory read executed at canonical unmapped virtual address `0xFFFFFFFF70000000` (Guard Page 1).

### Live QEMU Exception Diagnostics
```
[INFO]  === TRIGGERING ISOLATED HARDWARE RUNTIME TEST: PAGE FAULT (#PF) ===
[INFO]  Executing memory read at unmapped canonical virtual address 0xFFFFFFFF70000000...
[ERROR] ================================================================================
[ERROR] !!! CPU EXCEPTION: #PF (Page Fault) [Vector 14] !!!
[ERROR] ================================================================================
[ERROR] Vector                 : 14 (#PF)
[ERROR] Faulting Address (CR2) : 0xFFFFFFFF70000000
[ERROR] Active Page Table (CR3): 0x0000000000135000
[ERROR] Raw Error Code         : 0x0000000000000000
[ERROR]   Present (P)          : 0 (Non-present page)
[ERROR]   Access Type (W/R)    : 0 (Read access)
[ERROR]   Privilege Level (U/S): 0 (Supervisor (Kernel) mode)
[ERROR]   Reserved Bit (R/S)   : 0 (Normal)
[ERROR]   Instruction Fetch (I): 0 (Data access)
[ERROR]   Protection Key (PK)  : 0
[ERROR]   Shadow Stack (SS)    : 0
[ERROR]   SGX Violation        : 0
[ERROR] Interrupted CPU Registers:
[ERROR]   RIP: 0xFFFFFFFF80112B66 | CS : 0x0008 | RFLAGS: 0x0000000000000082
[ERROR]   RSP: 0xFFFFFFFF8012DF60 | SS : 0x0010
[ERROR]   RAX: 0x0000000000000000 | RBX: 0xFFFFFFFF70000000 | RCX: 0x0000000000000780 | RDX: 0x00000000000003D5
[ERROR]   RSI: 0x00000000000003D5 | RDI: 0x00000000000003D4 | RBP: 0xFFFFFFFF8012DFF0 | R8 : 0xFFFFFFFF800B9040
[ERROR]   R9 : 0x0000000000000009 | R10: 0xFFFFFFFF8012DF10 | R11: 0x0000000000000000 | R12: 0x0000000000000900
[ERROR]   R13: 0x0000000000000007 | R14: 0x0000000000000000 | R15: 0x0000000000000000
[ERROR] IST Execution Context:
[ERROR]   Handler Stack (RSP)  : 0xFFFFFFFF70009EE0 (IST2 Range: [0xFFFFFFFF70006000 - 0xFFFFFFFF7000A000], Confirmed in IST2: YES)
[ERROR] ================================================================================
[INFO]   [PASS] Hardware Runtime Verification: Page Fault (#PF, Vector 14) on IST2 Confirmed
```
- **Exit Signaling:** Executed `outb(0xF4, 0x14)`.
- **QEMU Process Exit Code:** `41` (`(0x14 << 1) | 1`).

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (Decoders validated against all architectural error code combinations).
- **Live QEMU Verification:** PASS (Actual CPU vector 14 dispatched, CR2 captured, IST2 stack confirmed, deterministic exit code 41).

---

## 9. Double Fault (#DF, Vector 8) Runtime Evidence

### Test Isolation & Trigger Mechanism
Triggered in dedicated isolated test mode (`test-df`) via boot argument `test-df`. `TSS.ist2` was pointed to unmapped guard page `0xFFFFFFFF70000000`. Access to unmapped address `0xFFFFFFFF7000A000` caused `#PF`. The CPU's attempt to push the `#PF` frame to the unmapped `IST2` address caused a second page fault during exception delivery, generating an authentic hardware Double Fault (`#DF`, Vector 8) as defined in Intel SDM Vol 3A Table 6-4.

### Live QEMU Exception Diagnostics
```
[INFO]  === TRIGGERING ISOLATED HARDWARE RUNTIME TEST: DOUBLE FAULT (#DF) ===
[INFO]  Corrupting IST2 stack pointer to unmapped guard page 0xFFFFFFFF70000000...
[INFO]  Triggering initial fault at unmapped address 0xFFFFFFFF7000A000...
[ERROR] ================================================================================
[ERROR] !!! CPU EXCEPTION: #DF (Double Fault) [Vector 8] !!!
[ERROR] ================================================================================
[ERROR] Vector                 : 8 (#DF)
[ERROR] Active Page Table (CR3): 0x0000000000135000
[ERROR] Raw Error Code         : 0x0000000000000000
[ERROR] Interrupted CPU Registers:
[ERROR]   RIP: 0xFFFFFFFF80112B08 | CS : 0x0008 | RFLAGS: 0x0000000000000082
[ERROR]   RSP: 0xFFFFFFFF8012DF60 | SS : 0x0010
[ERROR]   RAX: 0x0000000000000000 | RBX: 0xFFFFFFFF7000A000 | RCX: 0x0000000000000780 | RDX: 0x00000000000003D5
[ERROR] IST Execution Context:
[ERROR]   Handler Stack (RSP)  : 0xFFFFFFFF70004EE0 (IST1 Range: [0xFFFFFFFF70001000 - 0xFFFFFFFF70005000], Confirmed in IST1: YES)
[ERROR]   Dedicated IST1 Stack verified independent of regular kernel stack.
[ERROR] ================================================================================
[INFO]   [PASS] Hardware Runtime Verification: Double Fault (#DF, Vector 8) on IST1 Confirmed
```
- **Exit Signaling:** Executed `outb(0xF4, 0x08)`.
- **QEMU Process Exit Code:** `17` (`(0x08 << 1) | 1`).
- **Triple Fault Prevention:** Successfully contained without machine reset.

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS
- **Live QEMU Verification:** PASS (Actual hardware Double Fault delivered to IST1, isolated stack confirmed, deterministic exit code 17).

---

## 10. General Protection Fault (#GP, Vector 13) Runtime Evidence

### Test Isolation & Trigger Mechanism
Triggered in dedicated isolated test mode (`test-gp`) via boot argument `test-gp`. Executed `asm volatile("mov %0, %%ds" :: "r"(0x0028));`. Because selector `0x0028` refers to descriptor table index 5 (which exceeds GDT limit 4), the CPU generated an architectural `#GP(selector)`.

### Live QEMU Exception Diagnostics
```
[INFO]  === TRIGGERING ISOLATED HARDWARE RUNTIME TEST: GENERAL PROTECTION FAULT (#GP) ===
[INFO]  Loading invalid segment selector 0x0028 into DS register...
[ERROR] ================================================================================
[ERROR] !!! CPU EXCEPTION: #GP (General Protection Fault) [Vector 13] !!!
[ERROR] ================================================================================
[ERROR] Vector                 : 13 (#GP)
[ERROR] Active Page Table (CR3): 0x0000000000135000
[ERROR] Raw Error Code         : 0x0000000000000028
[ERROR]   External Event (EXT) : 0
[ERROR]   Table Indicator (TI) : 0 (GDT)
[ERROR]   IDT Reference (IDT)  : 0 (Descriptor Table)
[ERROR]   Selector Index       : 0x0005 (5)
[ERROR] Interrupted CPU Registers:
[ERROR]   RIP: 0xFFFFFFFF80112B36 | CS : 0x0008 | RFLAGS: 0x0000000000000082
[ERROR]   RSP: 0xFFFFFFFF8012DF60 | SS : 0x0010 | CR3   : 0x0000000000135000
[ERROR]   RAX: 0x0000000000000028 | RBX: 0x000000000011A4F0 | RCX: 0x0000000000000780 | RDX: 0x00000000000003D5
[ERROR]   RSI: 0x00000000000003D5 | RDI: 0x00000000000003D4 | RBP: 0xFFFFFFFF8012DFF0
[ERROR] ================================================================================
[INFO]   [PASS] Hardware Runtime Verification: General Protection Fault (#GP, Vector 13) Confirmed
```
- **Exit Signaling:** Executed `outb(0xF4, 0x13)`.
- **QEMU Process Exit Code:** `39` (`(0x13 << 1) | 1`).

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (`test_descriptors` verifies GPF decoder against bit patterns).
- **Live QEMU Verification:** PASS (Actual vector 13 received, raw error code `0x0028`, decoded selector index 5, TI 0 GDT, deterministic exit code 39).

---

## 11. PIC Evidence

### Architectural Specification
- Dual 8259 PIC initialized via standard 4-byte ICW sequence.
- Master PIC mapped to vector base `0x20` (`0x20..0x27`).
- Slave PIC mapped to vector base `0x28` (`0x28..0x2F`).
- All 16 IRQ lines initially masked (`0xFF` on ports `0x21` and `0xA1`).
- Spurious IRQ handling:
  - IRQ7: Reads Master ISR. If bit 7 is clear, logs spurious event and does not issue EOI.
  - IRQ15: Reads Slave ISR. If bit 7 is clear, sends EOI only to Master PIC.
- APIC Foundation:
  - CPUID `0x01` interrogation confirmed EDX bit 9 (APIC present).
  - `IA32_APIC_BASE` MSR (`0x1B`) read: Base=`0xFEE00000`, Global Enable=1.
  - **Explicit Note:** APIC detection is confirmed. Local APIC initialization is deferred to Phase 4/9.

### Live CPU State Capture
```
[INFO]  APIC Hardware Reconnaissance: Present=true, GlobalEnable=true, Base=0x00000000FEE00000
[INFO]  Remapping Dual 8259 PIC (Master: IRQ0..7 -> 0x20..0x27, Slave: IRQ8..15 -> 0x28..0x2F)...
[INFO]  Dual 8259 PIC successfully remapped and initialized (all IRQ lines masked).
[INFO]  PIC Hardware Verified: MasterMask=0xff, SlaveMask=0xff, SpuriousIRQ7=0, SpuriousIRQ15=0
[INFO]   [PASS] PIC/APIC Hardware Detection & Dual 8259 PIC Remapping
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS
- **Live QEMU Verification:** PASS

---

## 12. Timer Evidence

### Delivery Verification
The timer verification does not rely on software delay loops. It confirms actual hardware interrupt delivery:
1. Prior to interrupt enablement: `ticks_before = g_timer_ticks` (0).
2. `enable_interrupts()` (`sti`) is executed.
3. System enters bounded wait executing `halt()` waiting for hardware timer interrupts.
4. PIT IRQ0 generates vector 32. Central dispatcher invokes `handle_timer_interrupt()`, increments `g_timer_ticks`, and issues `PicManager::send_eoi(0)`.
5. Upon receiving at least 3 ticks, `disable_interrupts()` (`cli`) is executed.
6. Verification asserts `ticks_after > ticks_before`.

### Live QEMU Output
```
[INFO]  PIT Timer Initialized: Target=100 Hz, Divisor=11932 (0x2e9c), Command=0x34 (Mode 2 Rate Generator), IRQ0 unmasked.
[INFO]  Verifying Timer Interrupt delivery and periodic heartbeat...
[INFO]   [PASS] Timer Interrupt Foundation & Periodic Heartbeat (3 ticks received, tick=3)
[INFO]   [PASS] Timer Interrupt Foundation & Periodic Heartbeat Delivery
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS
- **Live QEMU Verification:** PASS (Actual IRQ0 delivered, counter advanced from 0 to 3 under STI).

---

## 13. Interrupt API Evidence

### Architectural Verification
`Timer::verify_interrupt_api()` exercises the complete low-level interrupt control interface with RFLAGS inspection:
1. Pre-condition: Checks `interrupts_enabled() == false`.
2. `enable_interrupts()` (`sti`): Confirms `interrupts_enabled() == true` (RFLAGS.IF set).
3. `save_and_disable_interrupts()`: Confirms `interrupts_enabled() == false` and saved flags contain IF bit.
4. `restore_interrupt_state(saved)`: Confirms `interrupts_enabled() == true`.
5. `disable_interrupts()` (`cli`): Confirms `interrupts_enabled() == false`.
6. Memory clobbers prevent compiler optimization and instruction hoisting across critical sections.

### Live QEMU Output
```
[INFO]   [PASS] Architecture Interrupt Control API (STI/CLI/Save/Restore confirmed)
[INFO]   [PASS] Architecture Interrupt Control API (STI/CLI/Save/Restore)
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS
- **Live QEMU Verification:** PASS

---

## 14. PMM Regression Evidence

### Memory Invariant Enforcement
Phase 3 allocates physical frames for:
- IST1 stack: 4 frames (16 KiB)
- IST2 stack: 4 frames (16 KiB)
- IST3 stack: 4 frames (16 KiB)
- TSS structure: 1 frame (4 KiB)
- VMM dynamic page tables for new mappings: dynamically allocated frames.

The PMM conservation equation was audited:
$$\text{free\_frames} + \text{allocated\_frames} == \text{usable\_frames}$$

All reserved frames are tracked strictly in `m_reserved_frames` and never conflated with usable or allocated frames.

### Live QEMU Output
```
[INFO]  Physical Memory Manager (PMM) Operational Statistics:
[INFO]    Managed RAM Range   : [0x0000000000000000 - 0x0000000020000000] (512 MiB)
[INFO]    PMM Bitmap Location : Physical 0x0000000000131000, Virtual 0xFFFFFFFF80131000 (16 KiB)
[INFO]    Total Page Frames   : 131072 frames (512 MiB)
[INFO]    Reserved Frames     : 1805 frames (7 MiB)
[INFO]    Usable Page Frames  : 129267 frames (504 MiB)
[INFO]    Free Frames         : 129267 frames (504 MiB)
[INFO]    Allocated Frames    : 0 frames (0 KiB)
...
[INFO]  Executing comprehensive PMM validation self-test suite (13 deterministic gates)...
[WARN]  PMM: Double-free rejected on physical address 0x0000000000149000 (frame 329)!
[ERROR] PMM: Attempted to free invalid or unaligned physical address: 0x0000000000001001
[ERROR] PMM: Attempted to free invalid or unaligned physical address: 0x0000000020000000
[ERROR] PMM: Attempted to free invalid or unaligned physical address: 0x0000000000000000
[WARN]  PMM: Attempt to free reserved physical memory at 0x0000000000090000 rejected!
[WARN]  PMM: Attempt to free reserved physical memory at 0x0000000000100000 rejected!
[WARN]  PMM: Attempt to free reserved physical memory at 0x0000000000130000 rejected!
[WARN]  PMM: Attempt to free reserved physical memory at 0x000000000011A000 rejected!
[WARN]  PMM: Attempt to free reserved physical memory at 0x0000000000131000 rejected!
[ERROR] PMM: Attempted to free invalid or unaligned physical address: 0xFFFFFFFFFFFFFFFF
[INFO]   [PERF] PMM end-to-end alloc_page latency : 62136 CPU cycles (includes 4 KiB zeroing)
[INFO]   [PERF] PMM end-to-end free_page latency  : 28374 CPU cycles
[INFO]  PMM validation self-test suite completed successfully (13/13 gates passed).
[INFO]   [PASS] Physical Memory Manager (PMM) Bitmap & Allocation Suite
```

### Verification Matrix
- **Configuration:** PASS
- **Host Verification:** PASS (17 tests, 369 assertions in `test_pmm`).
- **Live QEMU Verification:** PASS (13/13 runtime gates passed).

---

## 15. Host Unit Test Results

Executed via `make test-unit`:
- `test_parser`: 27 tests passed.
- `test_pmm`: 17 tests passed (369 assertions).
- `test_vmm`: 10 tests passed (611 assertions).
- `test_descriptors`: 7 tests passed (64 assertions).
- **Total Assertions Verified:** **1,071 assertions passed**, 0 failed.

---

## 16. BIOS Boot Test Results

Executed via `make test-bios`:
- Boot Mode: Legacy BIOS (SeaBIOS).
- Bootloader: GRUB Multiboot2.
- QEMU Parameters: `-m 512M -cdrom build/llamaos.iso -serial stdio -display none -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04`.
- Elapsed Time: 1.07s.
- Exit Code: `33` (`0x10 << 1 | 1`).
- All 24 mandatory assertion tokens confirmed: **PASS**.

---

## 17. UEFI Boot Test Results

Executed via `make test-uefi`:
- Boot Mode: 64-bit UEFI (OVMF firmware `/usr/share/ovmf/OVMF.fd`).
- Firmware Table: `UEFI System Table: Physical Addr=0x000000001FEDE018`.
- QEMU Parameters: `-m 512M -bios /usr/share/ovmf/OVMF.fd -cdrom build/llamaos.iso -serial stdio -display none -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04`.
- Elapsed Time: 3.79s.
- Exit Code: `33`.
- All mandatory assertion tokens confirmed: **PASS**.

---

## 18. Make vs CMake Parity

Both build environments were tested from scratch:

| Command | GNU Makefile Status | CMake / CTest Status |
|---|---|---|
| `test-unit` | **PASS** (4 suites, 1,071 assertions) | **PASS** (`ctest` 4/4 passed) |
| `test-bios` | **PASS** (Exit code 33) | **PASS** (Exit code 33) |
| `test-uefi` | **PASS** (Exit code 33) | **PASS** (Exit code 33) |
| `test-faults` | **PASS** (#PF=41, #GP=39, #DF=17) | **PASS** (#PF=41, #GP=39, #DF=17) |
| ISO Generation | `build/llamaos.iso` verified | `build-cmake/llamaos.iso` verified |

---

## 19. Remaining Limitations & Intentional Deferrals

1. **APIC Timer vs PIT:** Local APIC detection is complete via CPUID and MSR `0x1B`. APIC Timer calibration and IOAPIC configuration are intentionally deferred to Phase 4 / Phase 9 (SMP). The system operates on the verified Dual 8259 PIC and PIT 8254.
2. **Intermediate Page Table Reclamation:** Intermediate page tables created during VMM mappings remain allocated. Reclaiming empty intermediate page tables is deferred.
3. **Phase 4+ Boundaries:** Schedulers, user mode (Ring 3), system calls, filesystem, networking, general-purpose heap, and GUI are completely deferred.

---

## 20. Final Certification Status

| Subsystem Gate | Configuration Proof | Host Unit Proof | Live QEMU Proof | Gate Status |
|---|---|---|---|---|
| **Permanent GDT** | PASS | PASS | PASS | **VERIFIED** |
| **TSS & IST1..3** | PASS | PASS | PASS | **VERIFIED** |
| **IDT (256 Gates)** | PASS | PASS | PASS | **VERIFIED** |
| **Normalized ISR Stubs** | PASS | PASS | PASS | **VERIFIED** |
| **Breakpoint (#BP / INT3)**| PASS | PASS | PASS | **VERIFIED** |
| **Page Fault (#PF, Vec 14)**| PASS | PASS | PASS | **VERIFIED** |
| **Double Fault (#DF, Vec 8)**| PASS | PASS | PASS | **VERIFIED** |
| **General Protection (#GP)**| PASS | PASS | PASS | **VERIFIED** |
| **Dual 8259 PIC Remap** | PASS | PASS | PASS | **VERIFIED** |
| **PIT Timer (Mode 2 0x34)** | PASS | PASS | PASS | **VERIFIED** |
| **Interrupt Control API** | PASS | PASS | PASS | **VERIFIED** |
| **PMM/VMM Invariants** | PASS | PASS | PASS | **VERIFIED** |
| **BIOS Boot Harness** | PASS | N/A | PASS | **VERIFIED** |
| **UEFI Boot Harness** | PASS | N/A | PASS | **VERIFIED** |
| **Make & CMake Parity** | PASS | PASS | PASS | **VERIFIED** |

### Official Milestone Verdict
# **PHASE 3 VERIFIED**
All architectural requirements, live hardware exception proofs, and verification standards have been fully met with zero regressions and complete empirical QEMU evidence.
