#!/usr/bin/env python3
"""
LlamaOS/A - Memory Protection, Address Space Isolation & Syscall Security Test Suite
Verifies Blocker 2 (Hardware Memory Protection & Isolation on IST2) and
Blocker 3 (Syscall Pointer Validation).
"""

import os
import subprocess
import sys
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT_DIR)

TIMEOUT_SECONDS = 15

TEST_CASES = [
    {
        "name": "Hardware Memory Protection & Cross-Process Isolation (test-isolation)",
        "cmdline": "mode=test test-isolation",
        "expected_token": "[ISOLATION_TEST_PASS] Hardware memory protection, IST2 fault containment, and cross-process address space isolation verified.",
        "required_tokens": [
            "[ISOLATION_PF_CONTAINED]",
            "Hardware CR2 correctly identified"
        ],
        "expected_exit_code": 33
    },
    {
        "name": "Comprehensive Syscall Pointer Validation (test-syscall-security)",
        "cmdline": "mode=test test-syscall-security",
        "expected_token": "[SYSCALL_SECURITY_PASS] Syscall pointer bounds, canonical lower-half check, hardware page table validation, and overflow defenses verified.",
        "required_tokens": [
            "Syscall pointer bounds, canonical lower-half check"
        ],
        "expected_exit_code": 33
    }
]

def run_qemu_test(case):
    print(f"\n[RUN] Executing Security Test: {case['name']}...")

    temp_grub = f"""set timeout=0
set default=0
menuentry "LlamaOS/A Test" {{
    multiboot2 /boot/llamaos.elf {case['cmdline']}
    boot
}}
"""
    os.makedirs("build/iso_root/boot/grub", exist_ok=True)
    with open("build/iso_root/boot/grub/grub.cfg", "w") as f:
        f.write(temp_grub)

    subprocess.run(["grub-mkrescue", "-o", "build/test_sec.iso", "build/iso_root"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-cdrom", "build/test_sec.iso",
        "-serial", "stdio",
        "-display", "none",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"
    ]

    start_time = time.time()
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=TIMEOUT_SECONDS,
            text=True
        )
        output = proc.stdout
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as e:
        output = e.stdout if e.stdout else ""
        print(f"[FAIL] Test '{case['name']}' timed out after {TIMEOUT_SECONDS}s!")
        print("Output before timeout:\n", output)
        return False

    elapsed = time.time() - start_time

    if exit_code != case["expected_exit_code"]:
        print(f"[FAIL] Expected exit code {case['expected_exit_code']}, got {exit_code}")
        print("Output:\n", output)
        return False

    if case["expected_token"] not in output:
        print(f"[FAIL] Missing expected token: '{case['expected_token']}'")
        print("Output:\n", output)
        return False

    for req in case.get("required_tokens", []):
        if req not in output:
            print(f"[FAIL] Missing required token: '{req}'")
            print("Output:\n", output)
            return False

    print(f" [PASS] {case['name']} (exit_code={exit_code}, elapsed={elapsed:.2f}s)")
    return True

def main():
    print("=" * 70)
    print(" LlamaOS/A - Hardware Memory Protection & Syscall Security Suite")
    print("=" * 70)

    passed = 0
    for case in TEST_CASES:
        if run_qemu_test(case):
            passed += 1
        else:
            print(f"\n[ABORT] Test failed: {case['name']}")
            sys.exit(1)

    print("\n" + "=" * 70)
    print(f" Memory & Syscall Security Results: {passed}/{len(TEST_CASES)} PASSED")
    print("=" * 70)
    sys.exit(0)

if __name__ == "__main__":
    main()
