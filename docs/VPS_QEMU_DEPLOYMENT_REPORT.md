# LlamaOS/A - VPS QEMU/KVM Deployment Rehearsal Report

**Deployment Target**: DigitalOcean Droplet (Ubuntu 24.04.5 LTS Host)  
**Architecture Model**: Ubuntu HOST &rarr; QEMU/KVM Hypervisor &rarr; LlamaOS/A GUEST  
**Date of Rehearsal**: 2026-09-13  
**Status**: Completed  
**Final Verdict**: **A. VPS QEMU/KVM TEST PASSED**

---

## 1. Environment

### 1.1 Host Hardware & Kernel Specification
- **Hostname**: `ubuntu-s-4vcpu-8gb-240gb-intel-sgp1`
- **Linux Kernel**: `6.8.0-124-generic` (`#124-Ubuntu SMP PREEMPT_DYNAMIC Tue May 26 13:00:45 UTC 2026 x86_64`)
- **Operating System**: Ubuntu 24.04.5 LTS (Noble Numbat)
- **Virtualization Technology**: KVM Guest under DO Hypervisor (`systemd-detect-virt` returns `kvm`)
- **CPU Model**: `DO-Premium-Intel` (pc-i440fx-6.1 @ 2.0 GHz)
  - Sockets: 1, Cores: 4, Threads per core: 1 (Total: 4 vCPUs)
  - Address Sizes: 40 bits physical, 48 bits virtual
  - Flags: `fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx pdpe1gb rdtscp lm constant_tsc rep_good nopl xtopology cpuid tsc_known_freq pni pclmulqdq vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase bmi1 avx2 smep bmi2 erms invpcid rdseed adx smap clflushopt clwb xsaveopt xsavec xgetbv1 arat vnmi pku ospke arch_capabilities`
- **Host Memory**:
  - Total: 7.8 GiB (8,187,032 KiB)
  - Free / Available: 3.0 GiB free / 6.9 GiB available
  - Swap: 0 B
- **Host Storage**:
  - Primary Disk: `/dev/vda` (240 GB VirtIO Block Device)
  - Root Partition: `/dev/vda1` (232G total, 4.5G used, 227G available, 2% utilization)
  - EFI Partition: `/dev/vda15` mounted on `/boot/efi` (105M total, 6.2M used)
  - Boot Partition: `/dev/vda16` mounted on `/boot` (881M total, 117M used)

### 1.2 Toolchain & Compiler Environment
- **GCC**: `13.3.0` (`gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)
- **NASM**: `2.16.01`
- **GNU Binutils (ld)**: `2.42`
- **CMake**: `3.28.3`
- **QEMU Emulator**: `8.2.2` (`qemu-system-x86_64 version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)`)
- **OVMF Firmware**: `2024.02-2ubuntu0.9` (`/usr/share/ovmf/OVMF.fd`)

---

## 2. Artifact Manifest

Deployment artifacts built from a clean repository state (`make clean && make -j$(nproc)`).

| Property | Primary Boot Media (ISO) | Standalone Kernel (ELF) | Userland Executable (ELF) |
| :--- | :--- | :--- | :--- |
| **Artifact Name** | `llamaos.iso` | `llamaos.elf` | `init.elf` |
| **Source Path** | `/root/llamaos/build/llamaos.iso` | `/root/llamaos/build/llamaos.elf` | `/root/llamaos/build/init.elf` |
| **Isolated VPS Path** | `/root/llamaos-vps-test/llamaos.iso` | `/root/llamaos-vps-test/llamaos.elf` | `/root/llamaos-vps-test/init.elf` |
| **File Size** | 12,713,984 bytes (12.12 MiB) | 839,152 bytes (819.48 KiB) | 9,640 bytes (9.41 KiB) |
| **SHA-256 Hash** | `c491c2dedc47604c181d771024a69c3e15fd73490f2736bb89b28c94b46d0835` | `b46f0606729347439995d12b036a4480126087ef1dcad3a431ed78f4961e63cc` | `5b86dfc2f8639de02af07c336ff59ed1fab9cfaf203f5e84ee130644efcc06c5` |
| **Format / Type** | ISO 9660 CD-ROM (DOS/MBR hybrid bootable) | ELF 64-bit LSB statically linked, x86-64 | ELF 64-bit LSB statically linked, SYSV x86-64 |
| **Git Commit** | `1270a17b6a65b384b87fae8c5319c368847ae883` | `1270a17b6a65b384b87fae8c5319c368847ae883` | `1270a17b6a65b384b87fae8c5319c368847ae883` |
| **Build Timestamp** | 2026-09-13 06:16:05 UTC | 2026-09-13 06:12:28 UTC | 2026-09-13 06:12:28 UTC |

### 2.1 Disk Image Audit
A filesystem scan confirmed no raw disk images (`.raw`, `.qcow2`, `.img`) exist in the repository. The sole verified deployment media is the hybrid El Torito ISO image `llamaos.iso`.

### 2.2 SHA-256 Hash Matching Verification
```
c491c2dedc47604c181d771024a69c3e15fd73490f2736bb89b28c94b46d0835  /root/llamaos/build/llamaos.iso
c491c2dedc47604c181d771024a69c3e15fd73490f2736bb89b28c94b46d0835  /root/llamaos-vps-test/llamaos.iso
MATCH STATUS: IDENTICAL (100% Bit-for-bit parity confirmed)
```

---

## 3. KVM Detection & Hardware Acceleration Audit

1. **KVM Device Presence**:
   - Device node `/dev/kvm` exists: `crw-rw---- 1 root kvm 10, 232 Sep 12 00:56 /dev/kvm`
   - Test result: `KVM_PRESENT`
2. **Access Permissions**:
   - Current running user is `root` (member of `kvm` group).
   - Read/Write validation: `KVM_RW_OK`
3. **CPU Virtualization Capabilities**:
   - `Virtualization: VT-x`
   - `Virtualization type: full`
   - CPU flags expose `vmx`, `ept`, `vpid`, `flexpriority`, `tpr_shadow`
   - Nested virtualization is operational under the DigitalOcean KVM hypervisor.
4. **Acceleration Behavior**:
   - `qemu-system-x86_64 -enable-kvm` initialized successfully without kernel errors.
   - Non-fatal warning observed from QEMU: `host doesn't support requested feature: CPUID.80000001H:ECX.svm` (expected on Intel VT-x hosts where AMD SVM instructions are absent).
   - TCG software emulation fallback (`-accel tcg`) was also tested and validated with full functionality.

---

## 4. QEMU Configuration & Boundary Isolation

### 4.1 Host Safety Measures Enforced
- **Zero Host Disk Exposure**: Neither `-drive file=/dev/vda...` nor any raw host block devices were attached.
- **Isolated Test Directory**: Staged exclusively under `/root/llamaos-vps-test/`.
- **Immutable Boot Media**: Read-only ISO CD-ROM emulation (`-cdrom /root/llamaos-vps-test/llamaos.iso`).
- **No Host Disk Overwrite**: Host filesystem, `/boot/grub/grub.cfg`, and partition table were left completely untouched.

### 4.2 Standard QEMU Parameters
```bash
# BIOS Boot with KVM
qemu-system-x86_64 \
    -enable-kvm \
    -m 512M \
    -cdrom /root/llamaos-vps-test/llamaos.iso \
    -serial stdio \
    -display none \
    -no-reboot

# UEFI Boot with KVM & OVMF
qemu-system-x86_64 \
    -enable-kvm \
    -bios /usr/share/ovmf/OVMF.fd \
    -m 512M \
    -cdrom /root/llamaos-vps-test/llamaos.iso \
    -serial stdio \
    -display none \
    -no-reboot
```

---

## 5. BIOS Test Results

- **Bootloader**: GRUB 2.12 (i386-pc hybrid MBR boot sector)
- **Execution Target**: `multiboot2 /boot/llamaos.elf`
- **Result**: **PASS**
- **Milestones Verified**:
  - `[PASS] CPU Architecture & Security Extensions`
  - `[PASS] Freestanding Memory Primitives`
  - `[PASS] Higher-Half Runtime Execution & MMU Paging`
  - `[PASS] Linker Section & Memory Layout Sanity`
  - `[PASS] Bootloader Protocol & Bounds Integrity`
  - `[PASS] Physical Memory Map & Usable RAM Integrity` (511 MiB usable RAM)
  - `[PASS] Permanent Global Descriptor Table (GDT) & Segment Reload`
  - `[PASS] Task State Segment (TSS) & Dedicated IST Stacks`
  - `[PASS] Interrupt Descriptor Table (IDT 256 gates)`
  - `[PASS] CPU Exception Infrastructure & Breakpoint (#BP / INT3)`
  - `[PASS] Dual 8259 PIC Remapping (0x20..0x2F)`
  - `[PASS] PIT 8254 Timer & Periodic Heartbeat Delivery`
  - `[PASS] Device Registry & PCI Bus Enumeration (6 devices found)`
  - `[PASS] Kernel Thread Subsystem & Preemptive Scheduler`
  - `[PASS] 64-bit System Call Subsystem (SYSCALL/SYSRET)`
  - `[PASS] Minimal Userland Subsystem (ELF64 Loader, Ring 3 Transition, Processes)`
- **Kernel Boot Milestones**: Milestone 1, 2, 3, 4, 5, 6, 7 achieved cleanly.

---

## 6. UEFI Test Results

- **Firmware**: OVMF x86_64 (`/usr/share/ovmf/OVMF.fd`, 2024.02)
- **Bootloader**: GRUB 2.12 (x86_64-efi)
- **Firmware Detection**:
  - `UEFI System Table : Physical Addr=0x000000001F9EC018 (firmware-owned)`
  - `ACPI RSDP found : Revision=2, OEM='BOCHS ', XSDT PAddr=0x000000001F77D0E8`
- **Result**: **PASS**
- **Kernel Boot Milestones**: All Milestones 1 through 7 passed deterministically.
- **Automated Exit Code**: Code 33 (`(0x10 << 1) | 1` via `isa-debug-exit` port 0xF4).

---

## 7. Serial Console Observability Validation

All guest serial output was configured through COM1 (I/O base 0x3F8, 115200 baud, 8N1) routed to QEMU `-serial stdio`.

1. **Bootloader Output**:
   - GRUB serial support enabled via `serial --unit=0 --speed=115200` and `terminal_output serial console`.
   - Interactive menu entries rendered over serial console.
2. **Early Kernel Output**:
   - Higher-half 64-bit early serial driver initialized before page table switch.
   - Multiboot2 tag parser logs, CPUID dumps, and physical memory map entries streamed cleanly.
3. **Hardware Diagnostic Logs**:
   - Full 8042 PS/2 controller, keyboard vector 0x21, PCI bridge discovery, and linear framebuffer reports streamed over COM1.
4. **Ring 3 Userland Output**:
   - User mode output dispatched via `SYS_write` (vector syscall) to kernel console streamed cleanly to serial console:
     - `[USER_R3_ENTERED] Running in Ring 3 user space with CPL=3.`
     - `[USER_SYSCALL_OK] Basic system call invocation from Ring 3 confirmed.`
     - `[USER_GETPID_OK] Process ID query returned valid PID.`
     - `[USER_TICKS_OK] System timer tick count query returned valid counter.`
     - `[USER_PREEMPT_OK] Timer interrupts and user preemption survived.`
     - `[USER_YIELD_OK] Multiple cooperative SYS_yield cycles completed successfully.`
     - `[USER_EXIT_OK] Userland program reached clean termination point.`

---

## 8. Normal Boot Test (Interactive / Non-Test Mode)

- **Execution Command**: Booting `multiboot2 /boot/llamaos.elf` without `test` or debug arguments.
- **Bootloader Menu Selection**: Menuentry 1 (`LlamaOS/A`).
- **Kernel Boot Verification**:
  ```
  [INFO]  Normal boot mode initialized. Spawning init user process...
  [INFO]  Permanent GDT expanded for Phase 7 Userland (Ring 3):
  [INFO]    GDTR Limit    : 0x0037 (size: 56 bytes)
  [INFO]    User Data     : Selector 0x002b (DPL 3, Raw: 0x00cff2000000ffff)
  [INFO]    User Code     : Selector 0x0033 (DPL 3, Raw: 0x00affa000000ffff)
  [INFO]  ELF64 Loader: Executable successfully loaded into Ring 3 user space:
  [INFO]    Entry Point RIP : 0x0000000000400000
  [INFO]    User Stack Top  : 0x00007FFFFFFFE000 (16 KiB usable stack, 4 KiB guard page at 0x00007FFFFFFF9000)
  [INFO]    Total User Pages: 6 pages (24 KiB mapped)
  [INFO]  ProcessManager: Created user process 'init' (PID 1, Thread ID 3)
  [INFO]  Interactive console ready.
  [INFO]  System scheduler loop running (Interactive console worker active).
  [INFO]  Scheduler started: Preemptive multithreading active (PIT IRQ0 at 100 Hz).
  [INFO]  [USER_PROCESS] Launching user process 'init' (PID 1) in Ring 3 at 0x0000000000400000 (RSP=0x00007FFFFFFFE000)...
  [USER_R3_ENTERED] Running in Ring 3 user space with CPL=3.
  [USER_SYSCALL_OK] Basic system call invocation from Ring 3 confirmed.
  [USER_GETPID_OK] Process ID query returned valid PID.
  [USER_TICKS_OK] System timer tick count query returned valid counter.
  [INFO]  [PREEMPTION_EVENT #1] Tick 7: Thread 'init' (TID 3) preempted by PIT IRQ0
  [INFO]  [PREEMPTION_SWITCH] Switch from 'init' (TID 3) -> 'bootstrap' (TID 1)
  [INFO]  [PREEMPTION_SWITCH] Switch from 'bootstrap' (TID 1) -> 'init' (TID 3)
  [USER_PREEMPT_OK] Timer interrupts and user preemption survived.
  [USER_YIELD_OK] Multiple cooperative SYS_yield cycles completed successfully.
  [USER_EXIT_OK] Userland program reached clean termination point.
  [INFO]  [USER_PROCESS_EXIT] Process 'init' (PID 1) terminated with exit code 0
  ```
- **Architectural Limitations Documented**:
  - Interactive Shell: As designed for Phase 7, the `init` binary is a minimal verification user process testing CPL=3, system calls, preemption, and clean exit. The interactive shell REPL with persistent keyboard input buffer is scheduled for implementation in Phase 8.

---

## 9. Long-Run Stability Test

- **Duration**: 121.04 seconds continuous execution under KVM.
- **Resource Monitoring**: Sampled every 15 seconds via `/proc/<pid>/status`.
  - Initial Host RSS: 63.4 MB
  - Final Host RSS: 63.4 MB
  - RSS Memory Drift: **0.00 MB** (Zero memory leaks or growth detected)
  - Virtual Memory (VSZ): 987.0 MB constant
- **CPU & Interrupt Stability**:
  - PIT IRQ0 delivery remained regular at 100 Hz.
  - Periodic heartbeat and cooperative scheduling between `bootstrap` and `idle` threads remained active.
- **Fault Analysis**:
  - Kernel Panics: **0**
  - Unhandled CPU Exceptions (#GP, #PF, #DF): **0**
  - Triple Faults: **0**
  - Unexpected Reboots: **0**
  - Hangs / Deadlocks: **0**

---

## 10. Host Dependency Test

| Dependency Category | Guest Runtime Requirement | Host Interaction | Status |
| :--- | :--- | :--- | :--- |
| **Toolchain (GCC, NASM, Make)** | None | Used solely at build time | **PASSED** |
| **Interpreter (Python, Node.js)** | None | Zero guest runtime dependency | **PASSED** |
| **C Library (glibc, musl)** | None | Freestanding runtime built-in | **PASSED** |
| **Host Kernel APIs (Linux Syscalls)**| None | Executes on virtual bare metal | **PASSED** |
| **Host Filesystems** | None | Runs entirely within guest RAM & ISO | **PASSED** |

The LlamaOS/A guest is entirely self-contained within `llamaos.iso`.

---

## 11. Failures & Anomalies Classified

| Category | Classification | Description & Remediation |
| :--- | :--- | :--- |
| **HOST FAILURE** | None | Host environment stable, plenty of RAM/disk. |
| **QEMU/KVM FAILURE** | Non-fatal Warning | QEMU logged: `warning: host doesn't support requested feature: CPUID.80000001H:ECX.svm`. Expected on Intel VT-x hosts; KVM execution proceeded without error. |
| **BOOTLOADER FAILURE** | None | GRUB 2.12 cleanly boots both BIOS and UEFI. |
| **KERNEL FAILURE** | None | Zero kernel faults or regressions. |
| **USERLAND FAILURE** | None | Ring 3 process executed all syscalls and exited 0. |
| **SERIAL CONSOLE FAILURE** | Resolved | Bootloader serial menu was initially invisible due to missing GRUB serial terminal directives. Directives were integrated, enabling full serial observability. |
| **RESOURCE FAILURE** | None | Memory stayed strictly at 63.4 MB RSS. |
| **TOOLING FAILURE** | None | Unit tests (8/8) and CTest (8/8) passed 100%. |

---

## 12. Recovery & Safety Notes for Future Deployment Stages

> [!WARNING]
> Under **NO** circumstances should LlamaOS/A be written directly to host storage (`/dev/vda`, `/dev/sda`) or replace Ubuntu at this stage.

### Technical Checklist for Direct Bare Metal / VPS Host Replacement:
1. **Out-of-Band Console Access**:
   - DigitalOcean Web Console / VNC access required.
   - Serial recovery console access must be functional.
2. **Crash & Recovery Path**:
   - DigitalOcean Recovery ISO mode must be accessible if the bootloader fails.
   - Snapshot of Ubuntu host must be captured prior to any partition modifications.
3. **Filesystem & Storage Driver Maturity**:
   - LlamaOS/A currently operates from an in-memory Ramdisk / ISO9660.
   - Direct installation requires a stable VirtIO-block or NVMe driver with read-write filesystem support (Phase 8+).
4. **Bootloader Coexistence**:
   - Dual-booting or kexec chaining should be evaluated before destructive disk overwrites.

---

## 13. Final Verdict

### **A. VPS QEMU/KVM TEST PASSED**

- **KVM Status**: Operational (`/dev/kvm` present, readable, writable, VT-x nested virtualization active).
- **QEMU Version**: 8.2.2.
- **Artifact SHA-256**: `c491c2dedc47604c181d771024a69c3e15fd73490f2736bb89b28c94b46d0835` (Match verified).
- **BIOS Result**: PASSED (All 7 Milestones confirmed).
- **UEFI Result**: PASSED (OVMF firmware boot confirmed with Exit Code 33).
- **Serial Console**: PASSED (Full bi-directional observability from bootloader to Ring 3 userland).
- **Normal Boot**: PASSED (Clean Ring 3 entry, CPL=3, SYSCALL dispatch, exit code 0).
- **Stability**: PASSED (121.04s run, 0.00 MB memory drift, 0 panics, 0 triple faults).
- **Host Dependency**: PASSED (Zero host runtime dependencies).

*(Note: "production ready" or "cloud ready" claims are explicitly withheld in accordance with project engineering guidelines until storage, shell, and networking stacks are fully developed.)*
