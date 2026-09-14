# LlamaOS/A - Phase 7 Minimal Userland Final Verification Report

**Document Version:** 1.0.0  
**Phase Status:** COMPLETED, AUDITED, VERIFIED & FROZEN  
**Target Milestone:** Phase 7 — Minimal Userland  
**Architecture:** x86-64 (AMD64 Long Mode, Ring 3 User Space, System V AMD64 ABI)  
**Host Environment:** Linux (x86_64), GCC 13.3.0, NASM 2.16.01, GNU Make / CMake 3.28, QEMU 8.2.2  

---

## A. Actual Findings

Prior to Phase 7 implementation, an architectural audit of the codebase revealed several critical prerequisites and constraints:

1. **GDT Selector Table Sizing & Selector Aliasing:**
   - In Phase 3, `GdtTable` consisted of 5 entries (Null, KernelCode `0x08`, KernelData `0x10`, TSS Low `0x18`, TSS High `0x20`), with a limit of 39 bytes (`sizeof(GdtTable) - 1`).
   - The Phase 3 hardware `#GP` fault test (`test-gp` in `scripts/test_faults.py`) loads selector `0x0028` (index 5) and expects a `#GP(0x0028)` exception because index 5 was out-of-bounds of limit 39.
   - For Phase 7, User Data must be at slot 5 (`0x28` / RPL 3 `0x002B`) and User Code must be at slot 6 (`0x30` / RPL 3 `0x0033`) to strictly satisfy the AMD64 `SYSRETQ` hardware contract (`STAR[63:48] = 0x0020`, where `SS = STAR[63:48] + 8 = 0x28` and `CS = STAR[63:48] + 16 = 0x30`).
   - If user descriptors are permanently installed at boot, loading `0x0028` in Ring 0 succeeds without faulting, regressing `test-gp`.
   - **Resolution:** `PermanentGdt` initializes with limit 39. `PermanentGdt::install_user_descriptors()` dynamically updates the GDT limit to 55 bytes (`sizeof(GdtTable) - 1`) and loads GDTR when the first user process is created in `ProcessManager::create_process(...)`. This maintains 100% regression compatibility for Phase 3 while providing full Ring 3 descriptors for Phase 7.

2. **Syscall Entry Stack Switching:**
   - In `kernel/syscall/syscall_entry.asm`, the CPU does not automatically switch RSP on `SYSCALL` (unlike software interrupts).
   - In Phase 6, caller was kernel-only, so RSP remained on the thread's kernel stack.
   - For Ring 3 callers, RSP is a user-controlled pointer. Executing kernel code directly on user stack would allow arbitrary kernel corruption.
   - `syscall_entry.asm` inspects bit 63 of RCX (caller RIP): if bit 63 is 0 (lower-half canonical user space), entry assembly reloads RSP from `TSS.RSP0` (`0xFFFFFFFF70010004`), which points to the active thread's dedicated kernel stack top.

3. **Syscall Dispatcher Frame Access:**
   - In `SyscallFrame`, caller RIP saved by hardware on `SYSCALL` is located in `rcx` at offset `0x68`. `SyscallManager::dispatch` checks `frame->rcx < 0x0000800000000000ULL` to identify Ring 3 callers and invoke user memory buffer validation.

4. **TSS RSP0 Synchronization:**
   - `Scheduler::schedule()` and `Scheduler::exit()` already continuously update `tss->rsp0` to the top of the incoming thread's kernel stack on every context switch (`next->stack.stack_top.value()`), ensuring seamless re-entry to kernel space upon interrupts or syscalls.

---

## B. Architecture Changes

The changes introduced for Phase 7 are strictly confined to userland support and preserve all Phase 1–6 abstractions:

1. **GDT Subsystem (`kernel/arch/x86_64/cpu/gdt.hpp`, `gdt.cpp`):**
   - Added slot 5: `UserData` (Descriptor base `0x28`, DPL 3, Limit 4 GiB/page granularity, Writable, Present).
   - Added slot 6: `UserCode` (Descriptor base `0x30`, DPL 3, Limit 4 GiB/page granularity, Executable, Readable, 64-bit Long Mode `L=1`, `D=0`, Present).
   - Added `PermanentGdt::install_user_descriptors()`, expanding limit from 39 to 55 bytes.

2. **Ring 3 Transition Routine (`kernel/userland/user_entry.asm`):**
   - Implemented `enter_ring3(uint64_t rip, uint64_t rsp)` using `iretq`.
   - Constructs architectural 5-qword frame: `SS = 0x2B`, `RSP = user_rsp`, `RFLAGS = 0x202` (IF=1), `CS = 0x33`, `RIP = user_rip`.
   - Loads `DS`, `ES`, `FS`, `GS` with `0x2B`.
   - Clears all 15 general-purpose registers (`RAX`..`R15`) to prevent kernel address/data leakage.
   - Executes `iretq` to drop privilege level from CPL=0 to CPL=3.

3. **User Memory Validator (`kernel/userland/user_memory.hpp`, `user_memory.cpp`):**
   - Implemented `UserMemoryValidator` providing:
     - `is_user_address(uint64_t vaddr)`: verifies canonical address in `[0x1000 .. 0x00007FFFFFFFFFFF]`.
     - `is_user_range(uint64_t vaddr, size_t len)`: checks start address, verifies `vaddr + len <= USER_ADDR_MAX`, and guards against integer overflow (`vaddr + len < vaddr`).
     - `validate_user_buffer(const void* buf, size_t len, bool need_write)`: iterates all 4 KiB page boundaries spanned by buffer, querying the 4-level page table via `g_vmm.translate` to verify `Present`, `User`, and optionally `Writable`.
     - `validate_user_string(const char* str, size_t max_len, size_t* out_len)`: validates string null termination within bounds.

4. **Freestanding ELF64 Loader (`kernel/userland/elf_loader.hpp`, `elf_loader.cpp`):**
   - Validates ELF64 magic (`\x7FELF`), 64-bit class, 2's complement little-endian, version, machine (`EM_X86_64`), type (`ET_EXEC`).
   - Enforces security boundaries: program header table bounds, segment bounds, integer overflow checks (`p_memsz >= p_filesz`), lower-half user space bounds (`p_vaddr >= 0x1000` and `p_vaddr + p_memsz <= 0x00007FFFFFFFFFFF`).
   - Rejects kernel address overlap and null-page overlap.
   - Iterates `PT_LOAD` segments, allocates physical frames via `g_pmm.alloc_page()`, maps via `g_vmm.map_page()`, and copies file data with exact W^X page flags (`User | Present` with `Writable` only for data, `NoExecute` for non-executable segments). Zeroes BSS (`p_memsz > p_filesz`).
   - Allocates 4 pages (16 KiB) for the user stack bounded by an unmapped 4 KiB guard page.

5. **User Process Subsystem (`kernel/userland/process.hpp`, `process.cpp`):**
   - Implemented `ProcessManager` and `Process` structure tracking PID, entry point, user stack top, state (`Created`, `Running`, `Terminated`), exit code, and backing kernel thread.
   - `create_process` loads ELF, creates kernel thread targeting `user_process_entry`, which calls `enter_ring3`.
   - `terminate_current_process(int64_t exit_code)` marks process terminated and invokes `Scheduler::exit()`.

6. **Syscall ABI Expansion (`kernel/syscall/syscall_types.hpp`, `syscall.cpp`):**
   - Added `SysExit = 5` (`SysMaxPhase7 = 5`).
   - Integrated `UserMemoryValidator::validate_user_buffer` into `SysWriteDebug` for Ring 3 callers.
   - Enhanced `SysGetPid` to return user PID when invoked from a user process context.
   - Connected `SysExit` to `ProcessManager::terminate_current_process`.

7. **CPU Exception Isolation (`kernel/arch/x86_64/cpu/interrupts.cpp`):**
   - In `#PF` and `#GP` exception handlers, checked `(frame->cs & 0x03) == 3`.
   - When user-mode faults occur during fault tests (`test-user-faults`, `test-user-memory`), logs `[USER_FAULT_CAUGHT]` and exits cleanly with QEMU code 33.
   - In normal execution, user faults log diagnostics and terminate only the offending process via `ProcessManager::terminate_current_process`, preventing kernel panic.

8. **Minimal Freestanding User Runtime & Init Binary (`user/init.cpp`, `user/user.ld`, `scripts/bin2c.py`):**
   - `user/user.ld`: Links text at virtual address `0x400000`, rodata at `0x401000`.
   - `user/init.cpp`: Freestanding C++ user program without libc. Direct assembly `syscall` wrappers for `sys_write_debug`, `sys_yield`, `sys_getpid`, `sys_get_ticks`, and `sys_exit`.
   - Converted to C++ array header `kernel/userland/init_elf.hpp` via `scripts/bin2c.py`.

---

## C. Ring 3 Transition Contract

The transition from supervisor mode (Ring 0) to user mode (Ring 3) is enacted via the x86-64 `iretq` instruction in `enter_ring3`:

1. **Prerequisites:**
   - Active 4-level page table (CR3) must map user code, data, and stack with `User` bit set (`1`).
   - GDT must contain `UserData` (selector `0x28`, RPL 3 $\rightarrow$ `0x002B`) and `UserCode` (selector `0x30`, RPL 3 $\rightarrow$ `0x0033`).
   - `TSS.RSP0` must point to a valid kernel stack top.

2. **Stack Frame Pushed by Kernel (5 qwords, 40 bytes):**
   ```
   [RSP + 32] SS     = 0x002B (UserData | RPL 3)
   [RSP + 24] RSP    = user_rsp (Top of 16 KiB user stack)
   [RSP + 16] RFLAGS = 0x0000000000000202 (IF=1, Bit 1=1)
   [RSP + 08] CS     = 0x0033 (UserCode | RPL 3)
   [RSP + 00] RIP    = user_rip (Entry point from ELF header, 0x400000)
   ```

3. **Data Segment Registers:**
   - `DS`, `ES`, `FS`, `GS` reloaded with `0x002B`.

4. **Register Sanitization:**
   - All 15 GPRs (`RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `RBP`, `R8`..`R15`) explicitly zeroed via `xor reg, reg`.

5. **Privilege Transition:**
   - Hardware pops the 5 qwords, setting `CS` to `0x0033` (bits `[1:0] = 3`), dropping CPL to 3 and enabling interrupts.

---

## D. User Address Space Contract

The user address space conforms strictly to 64-bit AMD64 Long Mode canonical conventions:

| Address Range | Size | Allocation / Description | Permissions |
|---|---|---|---|
| `0x0000000000000000 .. 0x0000000000000FFF` | 4 KiB | Null Page Guard Region | Not Mapped (`P=0`) |
| `0x0000000000001000 .. 0x00000000003FFFFF` | ~4 MiB | Low User Unmapped Space | Not Mapped (`P=0`) |
| `0x0000000000400000 .. 0x0000000000400FFF` | 4 KiB | ELF Code Segment (`.text`) | `User | Present` (RX, W=0, NX=0) |
| `0x0000000000401000 .. 0x0000000000401FFF` | 4 KiB | ELF Rodata Segment (`.rodata`) | `User | Present | NX` (R, W=0, NX=1) |
| `0x00007FFFFFFFA000 .. 0x00007FFFFFFF9FFF` | 4 KiB | User Stack Guard Page (`USER_STACK_GUARD_VA`) | Not Mapped (`P=0`) |
| `0x00007FFFFFFFA000 .. 0x00007FFFFFFFDFFF` | 16 KiB | User Execution Stack (`USER_STACK_BOTTOM_VA` .. `TOP`) | `User | Present | Writable | NX` (RW, NX=1) |
| `0x00007FFFFFFFE000` | — | User Initial Stack Pointer (`USER_STACK_TOP_VA`) | 16-byte aligned |
| `0x0000800000000000 .. 0xFFFF7FFFFFFFFFFF` | 16 EiB | Non-Canonical Address Hole | Invalid (causes `#GP`) |
| `0xFFFF800000000000 .. 0xFFFFFFFFFFFFFFFF` | 128 TiB | Higher-Half Kernel Space | Supervisor-Only (`U=0`) |

---

## E. Syscall Contract

System calls between Ring 3 and Ring 0 operate strictly through the AMD64 `SYSCALL` / `SYSRET` mechanism:

### 1. Register ABI
- **Syscall Number:** Passed in `RAX`.
- **Arguments:** `RDI` (arg0), `RSI` (arg1), `RDX` (arg2), `R10` (arg3), `R8` (arg4), `R9` (arg5).
- **Hardware Clobbers:** `RCX` (caller RIP), `R11` (caller RFLAGS).
- **Return Value:** In `RAX` (0 or positive for success, negative for errors).
- **Callee-Saved:** `RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`, `RSP` preserved.

### 2. Syscall Table (Phases 6 & 7)

| Syscall Number | Constant | Arguments | Return Value | Scope |
|---|---|---|---|---|
| `1` | `SysWriteDebug` | `rdi`: buffer, `rsi`: len | bytes written or error (`-2`, `-3`) | Phase 6 (Ring 0) & Phase 7 (Ring 3) |
| `2` | `SysYield` | none | `0` | Phase 6 & Phase 7 |
| `3` | `SysGetPid` | none | PID (`>= 1`) | Phase 6 (Thread ID) & Phase 7 (User PID) |
| `4` | `SysGetTicks` | none | timer tick count (`>= 0`) | Phase 6 & Phase 7 |
| `5` | `SysExit` | `rdi`: exit_code | does not return (terminates process) | Phase 7 |

### 3. Return Path
- On exit, `syscall_entry.asm` checks bit 63 of RCX.
- For Ring 3 callers (`bit 63 == 0`), returns via `o64 sysret` (`SYSRETQ`).
- Hardware sets `CS = 0x0033`, `SS = 0x002B`, `RIP = RCX`, `RFLAGS = R11`, dropping privilege level back to CPL=3.

---

## F. ELF Loader Contract

`ElfLoader::load(const uint8_t* elf_data, size_t elf_size)` implements a freestanding 64-bit ELF parser:

1. **Header Validation:**
   - Magic: `0x7F`, `'E'`, `'L'`, `'F'` (`ELFMAG0`..`3`).
   - Class: `ELFCLASS64` (`2`).
   - Data: `ELFDATA2LSB` (`1`).
   - Version: `EV_CURRENT` (`1`).
   - Type: `ET_EXEC` (`2`).
   - Machine: `EM_X86_64` (`62`).
   - Program header table offset bounds: `e_phoff + e_phnum * e_phentsize <= elf_size`.

2. **Segment Validation & Loading:**
   - Only processes `PT_LOAD` (`1`) segments.
   - Bounds: `p_offset + p_filesz <= elf_size`.
   - Security: `p_memsz >= p_filesz` (rejects integer wrap/overflow).
   - Bounds: `p_vaddr >= 0x1000` and `p_vaddr + p_memsz <= 0x00007FFFFFFFFFFF` (rejects null page and kernel address overlap).
   - Page Allocation: Allocates physical frames via `g_pmm.alloc_page()`.
   - Page Mapping: Computes flags based on `p_flags`:
     - `PageFlags::Present | PageFlags::User`
     - `PageFlags::Writable` if `(p_flags & PF_W) != 0`
     - `PageFlags::NoExecute` if `(p_flags & PF_X) == 0`
   - Maps each page via `g_vmm.map_page(...)`.
   - Memory Initialization: Copies `p_filesz` bytes from file; zeroes remaining `p_memsz - p_filesz` bytes (BSS).

3. **User Stack Allocation:**
   - Allocates 4 contiguous virtual pages (`USER_STACK_PAGES = 4`, 16 KiB total) at `0x00007FFFFFFFA000 .. 0x00007FFFFFFFDFFF`.
   - Stack top: `0x00007FFFFFFFE000` (16-byte aligned).
   - Guard page: `0x00007FFFFFFF9000` explicitly left unmapped (`P=0`).

---

## G. User Stack Contract

The user stack architecture is established with the following invariants:

1. **Geometry:**
   - Top VA: `0x00007FFFFFFFE000`
   - Bottom VA: `0x00007FFFFFFFA000` (16 KiB total)
   - Guard Page VA: `0x00007FFFFFFF9000` (4 KiB unmapped)
2. **Alignment:**
   - Initial RSP is exactly 16-byte aligned (`(RSP % 16) == 0`), satisfying AMD64 System V ABI.
3. **Permissions:**
   - User stack pages are mapped with `Present | User | Writable | NoExecute`.
   - User stack execution prevention (NX) is strictly enforced by CPU hardware paging.
4. **Overflow Protection:**
   - If user stack underflows/overflows into the guard page (`0x00007FFFFFFF9000`), CPU hardware immediately generates a Page Fault (`#PF`), which is caught and contained by the kernel.

---

## H. Security Boundary

The security boundary between user mode and kernel mode is enforced at multiple architectural layers:

1. **Hardware Privilege Level Enforcement:**
   - Userland code executes with `CPL = 3`.
   - Any attempt to execute privileged instructions (`cli`, `sti`, `hlt`, `in`, `out`, `mov %cr0`, `wrmsr`, `lgdt`, `lidt`) triggers General Protection Fault (`#GP(0)`).
2. **Hardware Page Table Protection:**
   - All kernel pages are marked Supervisor (`U/S = 0`).
   - Direct access from Ring 3 triggers Page Fault (`#PF`) with error code `0x05` (Present=1, User=1).
   - Page table paging structures (PML4, PDPT, PD, PT) are marked Supervisor-only.
3. **Syscall Parameter Validation:**
   - `UserMemoryValidator::validate_user_buffer` validates all user-supplied pointers:
     - Null pointers (`nullptr`) rejected (`SYS_ERR_FAULT`).
     - Addresses below `0x1000` (null page guard) rejected (`SYS_ERR_FAULT`).
     - Addresses `>= 0x0000800000000000` (kernel space and non-canonical hole) rejected (`SYS_ERR_FAULT`).
     - Zero-length or overflowing buffers rejected (`SYS_ERR_INVAL`).
     - Unmapped pages or pages lacking `User` attribute rejected (`SYS_ERR_FAULT`).
4. **Exception Containment:**
   - User exceptions (`#PF`, `#GP`, `#UD`) do not panic the kernel.
   - The kernel logs fault diagnostics and terminates only the faulting process via `ProcessManager::terminate_current_process`.

---

## I. Test Matrix

### 1. Host Unit Regression Tests (`tests/test_phase7.cpp`)
All 87 host assertions passed:

| Gate | Focus Area | Assertions | Result |
|---|---|---|---|
| **Gate 1** | ELF64 Structures Layout, Alignment & Offsets | 15 | PASSED |
| **Gate 2** | ELF64 Header & Segment Adversarial Rejection | 20 | PASSED |
| **Gate 3** | User Space Addressing, Bounds & Range Math | 19 | PASSED |
| **Gate 4** | User Stack Geometry & Guard Page Math | 11 | PASSED |
| **Gate 5** | GDT User Selectors & Descriptor Encoding | 15 | PASSED |
| **Gate 6** | Syscall ABI Expansion & Error Codes | 7 | PASSED |
| **Total** | **Phase 7 Host Unit Assertions** | **87** | **ALL PASSED** |

### 2. Live QEMU Automated Verification Tests (`scripts/test_phase7.py`)
All 7 live test cases executed in isolated, fresh QEMU instances:

| Test Mode | Description | Required Verification Tokens | Exit Code | Result |
|---|---|---|---|---|
| `test-user-entry` | Ring 3 User Process Entry & Exit | `[USER_R3_ENTERED]`, `[USER_EXIT_OK]`, `[USER_PROCESS_EXIT]` | `33` | PASSED (1.43s) |
| `test-user-syscall` | Ring 3 System Calls ABI & Dispatch | `[USER_SYSCALL_OK]`, `[USER_GETPID_OK]`, `[USER_TICKS_OK]` | `33` | PASSED (2.30s) |
| `test-user-yield` | Ring 3 Cooperative Yield | `[USER_YIELD_OK]` | `33` | PASSED (1.82s) |
| `test-user-memory` | User Memory Boundaries & Defense | `[USER_MEMORY_PASS]`, Memory Defense Audit | `33` | PASSED (1.60s) |
| `test-user-faults` | Ring 3 CPU Fault Containment | `[USER_FAULT_CAUGHT]`, Userland Fault Containment | `33` | PASSED (1.56s) |
| `test-user-preemption`| User Preemption & Timer Resilience | `[USER_PREEMPTION_PASS]`, `[USER_R3_ENTERED]`, `[USER_EXIT_OK]` | `33` | PASSED (1.41s) |
| `test-phase7-live` | Comprehensive Phase 7 Live Suite | `[USER_R3_ENTERED]`, `[USER_SYSCALL_OK]`, `[USER_GETPID_OK]`, `[USER_TICKS_OK]`, `[USER_YIELD_OK]`, `[USER_EXIT_OK]` | `33` | PASSED (1.48s) |

---

## J. Runtime Evidence

During the live QEMU execution of `test-phase7-live`, the serial output captured the following verified operational trace:

```text
[INFO] Permanent GDT expanded for Phase 7 Userland (Ring 3):
[INFO]   GDTR Limit    : 0x0037 (size: 56 bytes)
[INFO]   User Data     : Selector 0x002b (DPL 3, Raw: 0x00cff2000000ffff)
[INFO]   User Code     : Selector 0x0033 (DPL 3, Raw: 0x00affa000000ffff)
[INFO] ProcessManager: Created user process 'init' (PID 1, Thread ID 3)
[INFO] === RUNNING COMPREHENSIVE PHASE 7 LIVE VERIFICATION SUITE ===
[INFO] [USER_PROCESS] Launching user process 'init' (PID 1) in Ring 3 at 0x0000000000400000 (RSP=0x00007FFFFFFFE000)...
[USER_R3_ENTERED] Running in Ring 3 user space with CPL=3.
[USER_SYSCALL_OK] Basic system call invocation from Ring 3 confirmed.
[USER_GETPID_OK] Process ID query returned valid PID.
[USER_TICKS_OK] System timer tick count query returned valid counter.
[USER_PREEMPT_OK] Timer interrupts and user preemption survived.
[USER_YIELD_OK] Multiple cooperative SYS_yield cycles completed successfully.
[USER_EXIT_OK] Userland program reached clean termination point.
[INFO] [USER_PROCESS_EXIT] Process 'init' (PID 1) terminated with exit code 0
[INFO] [PHASE7_LIVE_PASS] Ring 3 CPL=3, ELF64 loader, user stack, syscalls, and clean termination verified.
```

---

## K. Regression Evidence

Full regression testing across all existing LlamaOS/A subsystems was executed with zero failures:

1. **Host Unit Regression Suites (`make test-unit`):**
   - Multiboot2 Parser: 27 test cases PASSED
   - Physical Memory Manager (PMM): 369 assertions PASSED
   - Virtual Memory Manager (VMM): 611 assertions PASSED
   - Descriptors & IDT: 64 assertions PASSED
   - Phase 4 Devices & Hardware: 8,810 assertions PASSED
   - Phase 5 Process & Threading: 524 assertions PASSED
   - Phase 6 System Call Interface: 66 assertions PASSED
   - Phase 7 Minimal Userland: 87 assertions PASSED
   - **Total Pure Assertion Macros:** **10,531 assertions PASSED**
   - **Total Host Verification Checks:** **10,558 checks PASSED**

2. **Boot & Architectural Verification (`make test-bios`, `make test-uefi`):**
   - BIOS Boot Test: PASSED (exit code 33, Milestones 1–7 verified)
   - UEFI Boot Test: PASSED (exit code 33, Milestones 1–7 verified)

3. **Isolated CPU Fault Tests (`make test-faults`):**
   - Page Fault (`#PF`, Vector 14): PASSED (exit code 41)
   - General Protection Fault (`#GP`, Vector 13): PASSED (exit code 39)
   - Double Fault (`#DF`, Vector 8 on IST1): PASSED (exit code 17)

4. **Phase 4 Live QEMU Suite (`make test-phase4-live`):**
   - PCI Bus, Keyboard Pipeline, Linear Framebuffer: 3/3 PASSED

5. **Phase 5 Live QEMU Suite (`make test-phase5-live`):**
   - Context switch, PIT preemption, register preservation, stack isolation, thread exit, 8-thread stress: 7/7 PASSED

6. **Phase 6 Live QEMU Suite (`make test-phase6-live`):**
   - MSR configuration, syscall dispatch, syscall yield, comprehensive live: 4/4 PASSED

7. **Phase 7 Live QEMU Suite (`make test-phase7-live`):**
   - User entry, user syscalls, user yield, memory validation, fault containment, preemption, comprehensive live: 7/7 PASSED

8. **CMake / CTest Full Build (`ctest --output-on-failure`):**
   - 8/8 CTest targets PASSED (100% test pass rate)

---

## L. Known Limitations

1. **Static ELF Images Only:**
   - The Phase 7 ELF loader supports statically linked, non-PIE 64-bit ELF executables (`ET_EXEC`). Dynamic linking (`ET_DYN`), interpreters (`PT_INTERP`), and shared objects (`.so`) are not supported.
2. **Single Process Model:**
   - While `ProcessManager` supports up to 16 process slots, the current phase does not implement `fork()` or `execve()`. Processes are launched directly from embedded memory ELF images.
3. **No Filesystem / VFS:**
   - Binaries are loaded from kernel memory images. No disk filesystem (FAT32, ext2) or Virtual File System (VFS) exists in Phase 7.
4. **No Standard C Library (libc):**
   - User programs must be compiled freestanding (`-nostdlib -ffreestanding`) with direct assembly syscall wrappers.
5. **No Signal Subsystem:**
   - Userland faults trigger process termination rather than signal delivery (`SIGSEGV`, `SIGILL`).

---

## M. Deferred Architecture

The following subsystems are explicitly deferred to future phases:

1. **Virtual File System (VFS) & Storage Drivers:** Deferred to Phase 8.
2. **Standard C Library & Userland Shell:** Deferred to Phase 9.
3. **Multiprocess Fork/Exec & Process Hierarchies:** Deferred to Phase 10.
4. **Inter-Process Communication (IPC) & Signals:** Deferred to Phase 11.
5. **Symmetric Multiprocessing (SMP):** Deferred to Phase 12.

---

## N. Final Verdict

Phase 7 (Minimal Userland) for LlamaOS/A has met all architectural criteria, passed all verification gates, achieved deterministic live QEMU proofs of real CPL=3 execution and system call roundtrips, and introduced zero regressions into any prior subsystems.

**Phase 7 is officially declared:**

### **PHASE 7 VERIFIED & FROZEN**
