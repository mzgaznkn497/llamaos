#!/usr/bin/env python3
"""
LlamaOS/A - Autonomous QEMU Boot and Regression Test Runner
Launches the generated kernel ISO under QEMU for BIOS and UEFI environments,
captures serial console diagnostics, validates architectural assertions and banners,
and verifies deterministic completion.
"""

import sys
import os
import subprocess
import time
import argparse

COMMON_REQUIRED_TOKENS = [
    "LlamaOS/A",
    "Milestone 1: Bootable Foundation",
    "IA-32e Long Mode (64-bit)",
    "[PASS] CPU Architecture & Security Extensions",
    "[PASS] Freestanding Memory Primitives",
    "[PASS] Higher-Half Runtime Execution & MMU Paging",
    "[PASS] Linker Section & Memory Layout Sanity",
    "[PASS] Bootloader Protocol & Bounds Integrity",
    "[PASS] Command-Line Tokenizer & Boot Mode Integrity",
    "[PASS] Physical Memory Map & Usable RAM Integrity",
    "[PASS] Stack Protector Security Canary Active",
    "LlamaOS/A Kernel Boot Milestone 1 Accomplished Successfully!",
    "[PASS] Physical Memory Manager (PMM) Bitmap & Allocation Suite",
    "[PASS] Virtual Memory Manager (VMM) 4-Level Paging & Mapping Suite",
    "LlamaOS/A Kernel Boot Milestone 2 Accomplished Successfully!",
    "[PASS] Permanent Global Descriptor Table (GDT) & Segment Reload",
    "[PASS] Task State Segment (TSS) & Dedicated IST Stacks (PMM-backed, VMM-mapped)",
    "[PASS] Interrupt Descriptor Table (IDT 256 gates) & Vector Dispatcher",
    "[PASS] CPU Exception Infrastructure & Breakpoint (#BP / INT3) Verification",
    "[PASS] Page Fault (#PF) Diagnostics & IST Containment Handler",
    "[PASS] PIC/APIC Hardware Detection & Dual 8259 PIC Remapping",
    "[PASS] Architecture Interrupt Control API (STI/CLI/Save/Restore)",
    "[PASS] Timer Interrupt Foundation & Periodic Heartbeat Delivery",
    "LlamaOS/A Kernel Boot Milestone 3 Accomplished Successfully!"
]

def find_ovmf(custom_path: str = None) -> str:
    if custom_path and os.path.exists(custom_path):
        return custom_path
    candidates = [
        "/usr/share/ovmf/OVMF.fd",
        "/usr/share/OVMF/OVMF_CODE_4M.fd",
        "/usr/share/qemu/OVMF.fd"
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return ""

def run_test(iso_path: str, use_uefi: bool = False, ovmf_path: str = None, timeout_seconds: int = 20) -> bool:
    if not os.path.isfile(iso_path):
        print(f"[TEST ERROR] ISO file not found at: {iso_path}")
        return False

    qemu_cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-cdrom", iso_path,
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"
    ]

    mode_str = "BIOS"
    extra_tokens = []

    if use_uefi:
        resolved_ovmf = find_ovmf(ovmf_path)
        if not resolved_ovmf:
            print(f"[TEST WARN] OVMF firmware not found, skipping UEFI mode test.")
            return True
        qemu_cmd.extend(["-bios", resolved_ovmf])
        mode_str = f"UEFI ({resolved_ovmf})"
        extra_tokens.append("UEFI System Table")

    print(f"\n================================================================================")
    print(f" Starting LlamaOS/A Automated Boot Test [{mode_str}]")
    print(f" Command: {' '.join(qemu_cmd)}")
    print(f"================================================================================")

    start_time = time.time()
    proc = subprocess.Popen(
        qemu_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    stdout_data = ""
    try:
        stdout_data, stderr_data = proc.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout_data, stderr_data = proc.communicate()
        print(f"\n[TEST FAILED] Execution timed out after {timeout_seconds} seconds!")
        print("Captured Output:\n" + stdout_data)
        return False

    elapsed = time.time() - start_time
    print(stdout_data)

    # QEMU debug exit with value 0x10 yields exit code (0x10 << 1) | 1 = 33
    exit_code = proc.returncode
    print(f"--- QEMU Process Exited (code={exit_code}, elapsed={elapsed:.2f}s) ---")

    # Verify required tokens
    expected_tokens = list(COMMON_REQUIRED_TOKENS) + extra_tokens
    missing_tokens = []
    for token in expected_tokens:
        if token not in stdout_data:
            missing_tokens.append(token)

    if missing_tokens:
        print("\n[TEST FAILED] The following mandatory assertion tokens were missing:")
        for t in missing_tokens:
            print(f"  [-] '{t}'")
        return False

    # Check exit code:
    # 33 is expected from QEMU debug-exit (0x10 << 1 | 1).
    if exit_code != 33:
        if exit_code == 63:
            print(f"\n[TEST FAILED] Kernel Panicked! QEMU exited with panic debug code 63.")
        else:
            print(f"\n[TEST FAILED] Unexpected QEMU exit code: {exit_code} (expected 33)")
        return False

    print(f"\n[TEST PASSED] All architectural assertions and verification tokens confirmed for [{mode_str}].")
    return True

def main():
    parser = argparse.ArgumentParser(description="LlamaOS/A Boot Test Harness")
    parser.add_argument("iso", nargs="?", default="build/llamaos.iso", help="Path to kernel ISO image")
    parser.add_argument("--bios-only", action="store_true", help="Test BIOS mode only")
    parser.add_argument("--uefi-only", action="store_true", help="Test UEFI mode only")
    parser.add_argument("--ovmf", default=None, help="Custom path to OVMF firmware file")
    parser.add_argument("--timeout", type=int, default=20, help="Watchdog timeout in seconds")
    args = parser.parse_args()

    success = True
    if args.bios_only:
        success = run_test(args.iso, use_uefi=False, ovmf_path=args.ovmf, timeout_seconds=args.timeout)
    elif args.uefi_only:
        success = run_test(args.iso, use_uefi=True, ovmf_path=args.ovmf, timeout_seconds=args.timeout)
    else:
        # Run BIOS test first
        success = run_test(args.iso, use_uefi=False, ovmf_path=args.ovmf, timeout_seconds=args.timeout)
        if success:
            ovmf = find_ovmf(args.ovmf)
            if ovmf:
                success = run_test(args.iso, use_uefi=True, ovmf_path=ovmf, timeout_seconds=args.timeout)
            else:
                print("\n[TEST INFO] OVMF firmware not available; skipping UEFI test.")

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
