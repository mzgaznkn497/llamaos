#!/usr/bin/env python3
"""
LlamaOS/A - Phase 4 Device and Hardware Abstraction Live Verification Suite
Executes isolated hardware tests for PCI enumeration, Keyboard event pipeline,
and Linear Framebuffer inside QEMU.
"""

import os
import subprocess
import sys
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT_DIR)

ISO_PATH = "build/llamaos.iso"
TIMEOUT_SECONDS = 15

TEST_CASES = [
    {
        "name": "PCI Bus Enumeration (test-pci)",
        "cmdline": "mode=test test-pci",
        "expected_token": "[PCI_TEST_PASS] Discovered valid PCI devices successfully.",
        "expected_exit_code": 33
    },
    {
        "name": "Keyboard Event Pipeline (test-keyboard)",
        "cmdline": "mode=test test-keyboard",
        "expected_token": "[KEYBOARD_TEST_PASS] Scancode decode & event queue pipeline verified.",
        "expected_exit_code": 33
    },
    {
        "name": "Linear Framebuffer Subsystem (test-framebuffer)",
        "cmdline": "mode=test test-framebuffer",
        "expected_token": "[FRAMEBUFFER_TEST_PASS] Framebuffer test pattern rendered successfully.",
        "expected_exit_code": 33
    }
]

def run_qemu_test(case):
    print(f"\n[RUN] Executing Phase 4 Live Test: {case['name']}...")
    
    # Create temporary grub.cfg with the specific kernel arguments
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
    
    # Rebuild ISO with custom cmdline
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

    # Evaluate QEMU isa-debug-exit code
    # isa-debug-exit returns (val << 1) | 1. For val=0x10, exit code is 33.
    # When QEMU exits with status 33, bash/Python receives 33.
    token_found = case["expected_token"] in output
    code_match = (exit_code == case["expected_exit_code"])

    if token_found and code_match:
        print(f" [PASS] {case['name']} (exit_code={exit_code}, elapsed={elapsed:.2f}s)")
        return True
    else:
        print(f" [FAIL] {case['name']}")
        print(f"   Expected token present: {token_found} ('{case['expected_token']}')")
        print(f"   Exit code: {exit_code} (expected {case['expected_exit_code']})")
        print("\n--- Output ---\n", output)
        return False

import shutil

def main():
    kernel_elf = "build/llamaos.elf"
    if not os.path.exists(kernel_elf):
        if os.path.exists("build-cmake/llamaos.elf"):
            kernel_elf = "build-cmake/llamaos.elf"
            os.makedirs("build/iso_root/boot", exist_ok=True)
            shutil.copy(kernel_elf, "build/iso_root/boot/llamaos.elf")
        else:
            print("[ERROR] build/llamaos.elf not found. Run 'make kernel' first.")
            sys.exit(1)

    print("================================================================================")
    print(" LlamaOS/A - Phase 4 Device & Hardware Abstraction Live Verification Suite")
    print("================================================================================")

    all_passed = True
    for case in TEST_CASES:
        if not run_qemu_test(case):
            all_passed = False

    # Restore default grub.cfg
    subprocess.run(["make", "iso"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if os.path.exists("build/test_temp.iso"):
        os.remove("build/test_temp.iso")

    print("\n================================================================================")
    if all_passed:
        print(" [ALL PHASE 4 LIVE QEMU TESTS PASSED] (PCI, Keyboard, Framebuffer Verified)")
        print("================================================================================")
        sys.exit(0)
    else:
        print(" [SOME TESTS FAILED]")
        print("================================================================================")
        sys.exit(1)

if __name__ == "__main__":
    main()
