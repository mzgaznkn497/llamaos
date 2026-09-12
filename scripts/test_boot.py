#!/usr/bin/env python3
"""
LlamaOS/A - Autonomous QEMU Boot and Regression Test Runner
Launches the generated kernel ISO under QEMU, captures serial console diagnostics,
validates architectural assertions and banners, and verifies deterministic completion.
"""

import sys
import os
import subprocess
import time
import argparse

REQUIRED_TOKENS = [
    "LlamaOS/A",
    "Milestone 1: Bootable Foundation",
    "IA-32e Long Mode (64-bit)",
    "[PASS] CPU Architecture Verification",
    "[PASS] Freestanding Memory Primitives",
    "[PASS] Bootloader Protocol Handshake",
    "[PASS] Higher-Half Virtual Memory Layout",
    "LlamaOS/A Kernel Boot Milestone 1 Accomplished Successfully!"
]

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
    if use_uefi:
        if not ovmf_path:
            candidates = [
                "/usr/share/ovmf/OVMF.fd",
                "/usr/share/OVMF/OVMF_CODE_4M.fd"
            ]
            for c in candidates:
                if os.path.exists(c):
                    ovmf_path = c
                    break
        if not ovmf_path or not os.path.exists(ovmf_path):
            print(f"[TEST WARN] OVMF firmware not found, skipping UEFI mode test.")
            return True
        qemu_cmd.extend(["-bios", ovmf_path])
        mode_str = f"UEFI ({ovmf_path})"

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
    missing_tokens = []
    for token in REQUIRED_TOKENS:
        if token not in stdout_data:
            missing_tokens.append(token)

    if missing_tokens:
        print("\n[TEST FAILED] The following mandatory assertion tokens were missing:")
        for t in missing_tokens:
            print(f"  [-] '{t}'")
        return False

    # Check exit code:
    # 33 is expected from QEMU debug-exit (0x10 << 1 | 1).
    # Exit code 0 is also acceptable if QEMU terminates normally.
    if exit_code not in (33, 0):
        print(f"\n[TEST FAILED] Unexpected QEMU exit code: {exit_code} (expected 33 or 0)")
        return False

    print("\n[TEST PASSED] All architectural assertions and verification tokens confirmed.")
    return True

def main():
    parser = argparse.ArgumentParser(description="LlamaOS/A Boot Test Harness")
    parser.add_argument("iso", nargs="?", default="build/llamaos.iso", help="Path to kernel ISO image")
    parser.add_argument("--uefi", action="store_true", help="Test with UEFI firmware (OVMF)")
    parser.add_argument("--ovmf", default=None, help="Custom path to OVMF firmware file")
    parser.add_argument("--timeout", type=int, default=20, help="Watchdog timeout in seconds")
    args = parser.parse_args()

    success = run_test(args.iso, use_uefi=args.uefi, ovmf_path=args.ovmf, timeout_seconds=args.timeout)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
