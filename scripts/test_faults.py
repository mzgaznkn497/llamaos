#!/usr/bin/env python3
"""
LlamaOS/A - Isolated Hardware Exception Runtime Verification Harness
Generates targeted boot ISO configurations to deliberately trigger and verify
hardware CPU exceptions (#PF, #DF, #GP) in live QEMU execution.
"""

import os
import sys
import shutil
import tempfile
import subprocess
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_ELF = os.path.join(ROOT_DIR, "build", "llamaos.elf")

def run_isolated_test(name: str, cmdline_arg: str, expected_exit: int, expected_tokens: list) -> bool:
    print(f"\n================================================================================")
    print(f" Executing Isolated Hardware Fault Test: {name}")
    print(f" Kernel Argument: '{cmdline_arg}' | Expected QEMU Exit Code: {expected_exit}")
    print(f"================================================================================")

    if not os.path.isfile(KERNEL_ELF):
        print(f"[TEST ERROR] Kernel ELF missing at {KERNEL_ELF}!")
        return False

    with tempfile.TemporaryDirectory() as tmpdir:
        iso_root = os.path.join(tmpdir, "iso_root")
        boot_grub = os.path.join(iso_root, "boot", "grub")
        os.makedirs(boot_grub, exist_ok=True)

        shutil.copy(KERNEL_ELF, os.path.join(iso_root, "boot", "llamaos.elf"))

        grub_cfg = f"""set timeout=0
set default=0
insmod all_video

menuentry "{name}" {{
    multiboot2 /boot/llamaos.elf {cmdline_arg}
    boot
}}
"""
        with open(os.path.join(boot_grub, "grub.cfg"), "w") as f:
            f.write(grub_cfg)

        test_iso = os.path.join(tmpdir, "test.iso")
        mkrescue_cmd = ["grub-mkrescue", "-o", test_iso, iso_root]
        res = subprocess.run(mkrescue_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode != 0:
            print("[TEST ERROR] Failed to create test ISO via grub-mkrescue!")
            return False

        qemu_cmd = [
            "qemu-system-x86_64",
            "-m", "512M",
            "-cdrom", test_iso,
            "-serial", "stdio",
            "-display", "none",
            "-no-reboot",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"
        ]

        start_time = time.time()
        proc = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            stdout_data, stderr_data = proc.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout_data, stderr_data = proc.communicate()
            print(f"[TEST FAILED] Execution timed out after 15 seconds!")
            print("Captured Output:\n" + stdout_data)
            return False

        elapsed = time.time() - start_time
        print(stdout_data)
        print(f"--- QEMU Process Exited (code={proc.returncode}, elapsed={elapsed:.2f}s) ---")

        missing = []
        for t in expected_tokens:
            if t not in stdout_data:
                missing.append(t)

        if missing:
            print(f"\n[TEST FAILED] Missing expected tokens for {name}:")
            for m in missing:
                print(f"  [-] '{m}'")
            return False

        if proc.returncode != expected_exit:
            print(f"\n[TEST FAILED] Exit code mismatch: got {proc.returncode}, expected {expected_exit}")
            return False

        print(f"\n[TEST PASSED] {name} hardware runtime verification successful!")
        return True

def main():
    all_passed = True

    # 1. Page Fault (#PF, Vector 14)
    # Expected exit: (0x14 << 1) | 1 = 41
    pf_tokens = [
        "!!! CPU EXCEPTION: #PF (Page Fault) [Vector 14] !!!",
        "Vector                 : 14 (#PF)",
        "Faulting Address (CR2) : 0xFFFFFFFF70000000",
        "Raw Error Code         : 0x0000000000000000",
        "Present (P)          : 0 (Non-present page)",
        "Access Type (W/R)    : 0 (Read access)",
        "Privilege Level (U/S): 0 (Supervisor (Kernel) mode)",
        "Confirmed in IST2: YES",
        "[PASS] Hardware Runtime Verification: Page Fault (#PF, Vector 14) on IST2 Confirmed"
    ]
    if not run_isolated_test("Page Fault (#PF)", "test-pf", 41, pf_tokens):
        all_passed = False

    # 2. General Protection Fault (#GP, Vector 13)
    # Expected exit: (0x13 << 1) | 1 = 39
    gp_tokens = [
        "!!! CPU EXCEPTION: #GP (General Protection Fault) [Vector 13] !!!",
        "Vector                 : 13 (#GP)",
        "Raw Error Code         : 0x0000000000000028",
        "External Event (EXT) : 0",
        "Table Indicator (TI) : 0 (GDT)",
        "Selector Index       : 0x0005 (5)",
        "[PASS] Hardware Runtime Verification: General Protection Fault (#GP, Vector 13) Confirmed"
    ]
    if not run_isolated_test("General Protection Fault (#GP)", "test-gp", 39, gp_tokens):
        all_passed = False

    # 3. Double Fault (#DF, Vector 8)
    # Expected exit: (0x08 << 1) | 1 = 17
    df_tokens = [
        "!!! CPU EXCEPTION: #DF (Double Fault) [Vector 8] !!!",
        "Vector                 : 8 (#DF)",
        "Raw Error Code         : 0x0000000000000000",
        "Confirmed in IST1: YES",
        "Dedicated IST1 Stack verified independent of regular kernel stack.",
        "[PASS] Hardware Runtime Verification: Double Fault (#DF, Vector 8) on IST1 Confirmed"
    ]
    if not run_isolated_test("Double Fault (#DF)", "test-df", 17, df_tokens):
        all_passed = False

    if all_passed:
        print("\n================================================================================")
        print(" [ALL ISOLATED FAULT TESTS PASSED] (#PF, #GP, #DF live QEMU proofs confirmed)")
        print("================================================================================")
        sys.exit(0)
    else:
        print("\n[ISOLATED FAULT SUITE FAILED]")
        sys.exit(1)

if __name__ == "__main__":
    main()
