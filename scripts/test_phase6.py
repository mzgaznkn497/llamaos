#!/usr/bin/env python3
"""
LlamaOS/A - Phase 6 System Call Interface Live Verification Suite
Executes isolated hardware tests for SYSCALL/SYSRET MSR configuration,
real CPU SYSCALL execution, argument parsing, return codes, SYS_yield,
and comprehensive Phase 6 live verification.
"""

import os
import subprocess
import sys
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT_DIR)

TIMEOUT_SECONDS = 15

ALL_TEST_CASES = [
    {
        "name": "MSR Configuration & Initialization (test-syscall-init)",
        "cmdline": "mode=test test-syscall-init",
        "expected_token": "[SYSCALL_INIT_PASS] SYSCALL/SYSRET MSRs (STAR, LSTAR, SFMASK, EFER.SCE) verified.",
        "expected_exit_code": 33
    },
    {
        "name": "Real CPU SYSCALL Dispatch & ABI (test-syscall-dispatch)",
        "cmdline": "mode=test test-syscall-dispatch",
        "expected_token": "[SYSCALL_DISPATCH_PASS] Real CPU SYSCALL execution, arguments, and return values verified.",
        "expected_exit_code": 33
    },
    {
        "name": "SYS_yield & Scheduler Integration (test-syscall-yield)",
        "cmdline": "mode=test test-syscall-yield",
        "expected_token": "[SYSCALL_YIELD_PASS] SYS_yield cooperative context switching verified.",
        "expected_exit_code": 33
    },
    {
        "name": "Comprehensive Phase 6 Live Suite (test-phase6-live)",
        "cmdline": "mode=test test-phase6-live",
        "expected_token": "[PHASE6_LIVE_PASS] 64-bit system call interface, MSRs, dispatch, and ABI verified.",
        "expected_exit_code": 33
    }
]

def run_qemu_test(case):
    print(f"\n[RUN] Executing Phase 6 Live Test: {case['name']}...")

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

    subprocess.run(["grub-mkrescue", "-o", "build/test_temp.iso", "build/iso_root"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-cdrom", "build/test_temp.iso",
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
    token_found = case["expected_token"] in output
    code_match = (exit_code == case["expected_exit_code"])

    # Adversarial validation: Reject any negative failure tokens
    negative_failure_tokens = [
        "KPANIC",
        "Triple fault",
        "Triple Fault",
        "UNHANDLED CPU EXCEPTION",
        "Memory leak detected",
        "[SYSCALL_INIT_FAIL]",
        "[SYSCALL_DISPATCH_FAIL]",
        "[SYSCALL_YIELD_FAIL]",
        "[PHASE6_LIVE_FAIL]"
    ]
    found_negatives = [tok for tok in negative_failure_tokens if tok in output]
    no_negatives = (len(found_negatives) == 0)

    # Completeness verification: Kernel must boot through Milestone 6
    complete_boot = ("Milestone 6 Accomplished Successfully" in output) and ("LlamaOS/A" in output)

    # Specific check for dispatch test: must contain actual evidence
    dispatch_valid = True
    if "test-syscall-dispatch" in case["cmdline"]:
        dispatch_valid = ("Syscall Dispatch Evidence: nosys=-1, pid=1, write=" in output) and ("SYSCALL_DISPATCH_OK" in output)

    # Specific check for yield test: must contain worker logs
    yield_valid = True
    if "test-syscall-yield" in case["cmdline"]:
        yield_valid = ("[SYS_YIELD] Worker A iteration 1" in output) and ("[SYS_YIELD] Worker B iteration 1" in output)

    if token_found and code_match and no_negatives and complete_boot and dispatch_valid and yield_valid:
        print(f" [PASS] {case['name']} (exit_code={exit_code}, elapsed={elapsed:.2f}s)")
        return True
    else:
        print(f" [FAIL] {case['name']}")
        print(f"   Expected token present: {token_found} ('{case['expected_token']}')")
        print(f"   Exit code: {exit_code} (expected {case['expected_exit_code']})")
        print(f"   No negative failure tokens: {no_negatives} (found: {found_negatives})")
        print(f"   Complete boot verified: {complete_boot}")
        if "test-syscall-dispatch" in case["cmdline"]:
            print(f"   Dispatch evidence verified: {dispatch_valid}")
        if "test-syscall-yield" in case["cmdline"]:
            print(f"   Yield trace verified: {yield_valid}")
        print("\n--- Output ---\n", output)
        return False

def main():
    filter_arg = sys.argv[1] if len(sys.argv) > 1 else None

    if filter_arg:
        cases = [c for c in ALL_TEST_CASES if filter_arg in c["cmdline"]]
        if not cases:
            print(f"Unknown test filter: '{filter_arg}'")
            sys.exit(1)
    else:
        cases = ALL_TEST_CASES

    print("=" * 70)
    print(" LlamaOS/A - Phase 6 System Call Interface Live Verification Suite")
    print("=" * 70)

    all_passed = True
    passed_count = 0
    for case in cases:
        if run_qemu_test(case):
            passed_count += 1
        else:
            all_passed = False

    # Restore default grub.cfg
    subprocess.run(["make", "iso"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print("\n" + "=" * 70)
    print(f" Phase 6 Live Test Results: {passed_count}/{len(cases)} PASSED")
    print("=" * 70)

    if not all_passed:
        sys.exit(1)

if __name__ == "__main__":
    main()
