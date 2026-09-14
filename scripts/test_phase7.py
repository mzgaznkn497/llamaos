#!/usr/bin/env python3
"""
LlamaOS/A - Phase 7 Minimal Userland Live Verification Suite
Executes isolated hardware and live QEMU tests for Ring 3 CPL=3 entry,
ELF64 loading, user stack allocation with guard page, system call dispatch
from user space, user memory validation, CPU fault containment, and clean process exit.
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
        "name": "Ring 3 User Process Entry & Exit (test-user-entry)",
        "cmdline": "mode=test test-user-entry",
        "expected_token": "[USER_ENTRY_PASS] Ring 3 user process entry and clean exit verified.",
        "required_tokens": ["[USER_R3_ENTERED]", "[USER_EXIT_OK]", "[USER_PROCESS_EXIT]"],
        "expected_exit_code": 33
    },
    {
        "name": "Ring 3 System Calls ABI & Dispatch (test-user-syscall)",
        "cmdline": "mode=test test-user-syscall",
        "expected_token": "[USER_SYSCALL_PASS] Ring 3 system calls (write, getpid, get_ticks) verified.",
        "required_tokens": ["[USER_SYSCALL_OK]", "[USER_GETPID_OK]", "[USER_TICKS_OK]"],
        "expected_exit_code": 33
    },
    {
        "name": "Ring 3 Cooperative Yield (test-user-yield)",
        "cmdline": "mode=test test-user-yield",
        "expected_token": "[USER_YIELD_PASS] Ring 3 cooperative multitasking (SYS_yield) verified.",
        "required_tokens": ["[USER_YIELD_OK]"],
        "expected_exit_code": 33
    },
    {
        "name": "User Memory Validation & Defense (test-user-memory)",
        "cmdline": "mode=test test-user-memory",
        "expected_token": "[USER_MEMORY_PASS] User memory boundaries, null guard, kernel space isolation, and validation verified.",
        "required_tokens": ["Memory Defense Audit"],
        "expected_exit_code": 33
    },
    {
        "name": "Ring 3 CPU Fault Containment (test-user-faults)",
        "cmdline": "mode=test test-user-faults",
        "expected_token": "[USER_FAULT_CAUGHT]",
        "required_tokens": ["Userland Fault Containment"],
        "expected_exit_code": 33
    },
    {
        "name": "User Preemption & Timer Resilience (test-user-preemption)",
        "cmdline": "mode=test test-user-preemption",
        "expected_token": "[USER_PREEMPTION_PASS] User mode preemption and interrupt return verified.",
        "required_tokens": ["[USER_R3_ENTERED]", "[USER_PREEMPT_OK]", "[USER_EXIT_OK]"],
        "expected_exit_code": 33
    },
    {
        "name": "Comprehensive Phase 7 Live Suite (test-phase7-live)",
        "cmdline": "mode=test test-phase7-live",
        "expected_token": "[PHASE7_LIVE_PASS] Ring 3 CPL=3, ELF64 loader, user stack, syscalls, and clean termination verified.",
        "required_tokens": [
            "[USER_R3_ENTERED]",
            "[USER_SYSCALL_OK]",
            "[USER_GETPID_OK]",
            "[USER_TICKS_OK]",
            "[USER_PREEMPT_OK]",
            "[USER_YIELD_OK]",
            "[USER_EXIT_OK]"
        ],
        "expected_exit_code": 33
    }
]

def run_qemu_test(case):
    print(f"\n[RUN] Executing Phase 7 Live Test: {case['name']}...")

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

    # Verify all required tokens
    missing_tokens = [tok for tok in case.get("required_tokens", []) if tok not in output]

    # Adversarial validation: Reject any negative failure tokens
    negative_failure_tokens = [
        "KPANIC",
        "Triple fault",
        "Triple Fault",
        "UNHANDLED CPU EXCEPTION",
        "Memory leak detected",
        "[USER_ENTRY_FAIL]",
        "[USER_SYSCALL_FAIL]",
        "[USER_YIELD_FAIL]",
        "[USER_MEMORY_FAIL]",
        "[USER_PREEMPTION_FAIL]",
        "[PHASE7_LIVE_FAIL]"
    ]
    found_negatives = [tok for tok in negative_failure_tokens if tok in output]
    no_negatives = (len(found_negatives) == 0)

    # Completeness verification: Kernel must boot through Milestone 7
    complete_boot = ("Milestone 7 Accomplished Successfully" in output) and ("LlamaOS/A" in output)

    if token_found and code_match and (len(missing_tokens) == 0) and no_negatives and complete_boot:
        print(f" [PASS] {case['name']} (exit_code={exit_code}, elapsed={elapsed:.2f}s)")
        return True
    else:
        print(f" [FAIL] {case['name']}")
        print(f"   Expected token present: {token_found} ('{case['expected_token']}')")
        print(f"   Missing required tokens: {missing_tokens}")
        print(f"   Exit code: {exit_code} (expected {case['expected_exit_code']})")
        print(f"   No negative failure tokens: {no_negatives} (found: {found_negatives})")
        print(f"   Complete boot verified: {complete_boot}")
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
    print(" LlamaOS/A - Phase 7 Minimal Userland Live Verification Suite")
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
    print(f" Phase 7 Live Test Results: {passed_count}/{len(cases)} PASSED")
    print("=" * 70)

    if not all_passed:
        sys.exit(1)

if __name__ == "__main__":
    main()
