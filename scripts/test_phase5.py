#!/usr/bin/env python3
"""
LlamaOS/A - Phase 5 Process & Threading Live Verification Suite
Executes isolated hardware tests for cooperative context switching,
PIT IRQ0 preemptive scheduling, callee-saved register preservation,
stack isolation/guard pages, thread exit/cleanup, and 8-thread concurrency stress.
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
        "name": "Cooperative Context Switching (test-scheduler)",
        "cmdline": "mode=test test-scheduler",
        "expected_token": "[TEST_SCHEDULER_PASS] Cooperative context switching verified with interleaving.",
        "expected_exit_code": 33
    },
    {
        "name": "Preemptive Multitasking via PIT IRQ0 (test-preemption)",
        "cmdline": "mode=test test-preemption",
        "expected_token": "[PREEMPTION_TEST_PASS] Preemptive multitasking verified via PIT IRQ0.",
        "expected_exit_code": 33
    },
    {
        "name": "Callee-Saved Register Preservation (test-context)",
        "cmdline": "mode=test test-context",
        "expected_token": "[REGISTER_PRESERVATION_PASS] Callee-saved registers RBX, RBP, R12, R13, R14, R15 preserved across context switches.",
        "expected_exit_code": 33
    },
    {
        "name": "Stack Isolation & Guard Pages (test-stack)",
        "cmdline": "mode=test test-stack",
        "expected_token": "[STACK_ISOLATION_PASS] Thread stacks isolated and non-overlapping with guard pages.",
        "expected_exit_code": 33
    },
    {
        "name": "Thread Termination & Deferred Reclamation (test-thread-exit)",
        "cmdline": "mode=test test-thread-exit",
        "expected_token": "[THREAD_EXIT_PASS] Thread exit and deferred cleanup verified.",
        "expected_exit_code": 33
    },
    {
        "name": "8-Thread Concurrency Stress Test (test-thread-stress)",
        "cmdline": "mode=test test-thread-stress",
        "expected_token": "[STRESS_TEST_PASS] 8-thread concurrent workload completed successfully.",
        "expected_exit_code": 33
    },
    {
        "name": "Comprehensive Phase 5 Live Suite (test-phase5-live)",
        "cmdline": "mode=test test-phase5-live",
        "expected_token": "[PHASE5_LIVE_PASS] Kernel threading, stack isolation, and TCB validation successful.",
        "expected_exit_code": 33
    }
]

def run_qemu_test(case):
    print(f"\n[RUN] Executing Phase 5 Live Test: {case['name']}...")

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
        "[STACK_ISOLATION_FAIL]",
        "[REGISTER_PRESERVATION_FAIL]",
        "[PREEMPTION_TEST_FAIL]",
        "[TEST_SCHEDULER_FAIL]",
        "[THREAD_EXIT_FAIL]",
        "[STRESS_TEST_FAIL]",
        "[PHASE5_LIVE_FAIL]"
    ]
    found_negatives = [tok for tok in negative_failure_tokens if tok in output]
    no_negatives = (len(found_negatives) == 0)

    # Completeness verification: Kernel must boot through Milestone 5
    complete_boot = ("Milestone 5 Accomplished Successfully" in output) and ("LlamaOS/A" in output)

    # Specific check for preemption test: Must have actual preemption events and evidence
    preemption_valid = True
    if "test-preemption" in case["cmdline"]:
        preemption_valid = ("[PREEMPTION_EVENT #" in output) and ("[PREEMPTION_SWITCH]" in output) and ("[PREEMPTION_EVIDENCE]" in output)

    if token_found and code_match and no_negatives and complete_boot and preemption_valid:
        print(f" [PASS] {case['name']} (exit_code={exit_code}, elapsed={elapsed:.2f}s)")
        return True
    else:
        print(f" [FAIL] {case['name']}")
        print(f"   Expected token present: {token_found} ('{case['expected_token']}')")
        print(f"   Exit code: {exit_code} (expected {case['expected_exit_code']})")
        print(f"   No negative failure tokens: {no_negatives} (found: {found_negatives})")
        print(f"   Complete boot verified: {complete_boot}")
        if "test-preemption" in case["cmdline"]:
            print(f"   Preemption trace verified: {preemption_valid}")
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
    print(" LlamaOS/A - Phase 5 Process & Threading Live Verification Suite")
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
    print(f" Phase 5 Live Test Results: {passed_count}/{len(cases)} PASSED")
    print("=" * 70)

    if not all_passed:
        sys.exit(1)

if __name__ == "__main__":
    main()
