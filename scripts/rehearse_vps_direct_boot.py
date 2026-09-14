#!/usr/bin/env python3
"""
LlamaOS/A - Complete VPS Direct-Boot Master Rehearsal Suite
Executes end-to-end production verification matching DigitalOcean VPS specs:
1. Storage Persistence & Filesystem CRUD Rehearsal
2. Direct-Boot GPT Disk & Ring 3 Interactive Shell Rehearsal
3. VirtIO-Net TCP/IP Stack & Remote Management Rehearsal
"""

import os
import subprocess
import sys
import time

def run_suite(name: str, script_path: str) -> bool:
    print("\n" + "=" * 76)
    print(f" >>> STARTING REHEARSAL PHASE: {name}")
    print("=" * 76)
    t0 = time.time()
    res = subprocess.run([sys.executable, script_path], capture_output=False)
    elapsed = time.time() - t0
    if res.returncode == 0:
        print(f"\n[REHEARSAL SUCCESS] {name} passed in {elapsed:.2f}s")
        return True
    else:
        print(f"\n[REHEARSAL FAILURE] {name} failed with exit code {res.returncode}")
        return False

def main():
    print("*" * 76)
    print("  LLAMAOS/A — DIGITALOCEAN VPS DIRECT-BOOT MASTER REHEARSAL")
    print("  Target: Pure Bare-Metal GPT VirtIO Storage & Networking Runtime")
    print("*" * 76)

    t_start = time.time()

    suites = [
        ("Storage Persistence & FAT32 Rehearsal", "scripts/test_storage_persistence.py"),
        ("Direct-Boot GPT Disk & Userland Shell Rehearsal", "scripts/test_interactive_shell.py"),
        ("VirtIO-Net TCP Remote Management Console Rehearsal", "scripts/test_network.py")
    ]

    results = []
    for name, path in suites:
        success = run_suite(name, path)
        results.append((name, success))
        if not success:
            break

    total_time = time.time() - t_start

    print("\n" + "=" * 76)
    print("               MASTER REHEARSAL VERIFICATION SUMMARY")
    print("=" * 76)
    all_passed = True
    for name, success in results:
        status = "PASSED" if success else "FAILED"
        print(f" - {name:<55} : [{status}]")
        if not success:
            all_passed = False

    print("-" * 76)
    print(f"Total Rehearsal Duration: {total_time:.2f}s")
    if all_passed and len(results) == len(suites):
        print(" >>> 100% PRODUCTION VERDICT: ALL DIRECT-BOOT REHEARSAL GATES PASSED <<<")
        print("=" * 76)
        return 0
    else:
        print(" >>> PRODUCTION VERDICT: REHEARSAL FAILED <<<")
        print("=" * 76)
        return 1

if __name__ == "__main__":
    sys.exit(main())
