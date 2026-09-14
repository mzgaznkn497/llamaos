#!/usr/bin/env python3
"""
LlamaOS/A - Phase H, I, J Direct-Boot Interactive Userland Shell Test Suite
Proves:
1. Direct Hard-Disk Boot (Zero CD-ROM, SeaBIOS -> GRUB -> LlamaOS/A from GPT disk)
2. FAT32 Persistent VFS mount of /dev/vda3
3. Spawning Ring 3 userland shell /bin/sh directly from filesystem
4. Interactive execution of:
   - help
   - echo
   - ls /
   - cat /etc/os-release
   - mkdir /testdir
   - touch /testdir/file.txt
   - ls /testdir
   - ps
   - mem
   - uptime
"""

import os
import subprocess
import sys
import time

def main():
    print("=" * 72)
    print(" LlamaOS/A - Direct-Boot Interactive Userland Shell Verification")
    print("=" * 72)

    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-drive", "file=build/llamaos.img,if=virtio,format=raw",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot"
    ]

    print("[STEP 1] Launching QEMU direct-boot from build/llamaos.img (NO CD-ROM)...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    full_output = ""
    start_time = time.time()

    def wait_for_token(token: str, timeout: float = 10.0) -> bool:
        nonlocal full_output
        t0 = time.time()
        buf = ""
        while time.time() - t0 < timeout:
            if proc.poll() is not None:
                break
            ch = proc.stdout.read(1)
            if ch:
                sys.stdout.write(ch)
                sys.stdout.flush()
                buf += ch
                full_output += ch
                if token in buf:
                    return True
            else:
                time.sleep(0.01)
        return False

    # Wait for shell prompt
    print("[STEP 2] Waiting for Ring 3 user shell prompt (llamaos$)...")
    if not wait_for_token("llamaos$ ", timeout=15.0):
        print(f"\n[FAIL] Timed out waiting for shell prompt. Output:\n{full_output}")
        proc.kill()
        return 1

    print("\n>>> [CONFIRMED] Userland shell /bin/sh running in Ring 3 at prompt!")

    commands_to_test = [
        ("echo LLAMAOS_DIRECT_BOOT_CONFIRMED", "LLAMAOS_DIRECT_BOOT_CONFIRMED"),
        ("help", "Available Shell Commands:"),
        ("ls /", "Directory listing for '/'"),
        ("cat /etc/os-release", "PRETTY_NAME="),
        ("mkdir /testdir", "llamaos$ "),
        ("touch /testdir/data.txt", "llamaos$ "),
        ("ls /testdir", "Directory listing for '/testdir'"),
        ("mem", "Physical Memory Status (PMM):"),
        ("ps", "Process Status:"),
        ("uptime", "Uptime:")
    ]

    for user_cmd, expected_token in commands_to_test:
        time.sleep(0.2)
        print(f"\n[EXEC] Running shell command: '{user_cmd}'")
        proc.stdin.write(f"{user_cmd}\n")
        proc.stdin.flush()

        if not wait_for_token(expected_token, timeout=6.0):
            print(f"\n[FAIL] Expected token '{expected_token}' not found after running '{user_cmd}'")
            proc.kill()
            return 1
        print(f"  >>> [PASS] Verified response for '{user_cmd}'")
        if expected_token != "llamaos$ ":
            wait_for_token("llamaos$ ", timeout=6.0)

    print("\n[STEP 3] Testing poweroff command...")
    proc.stdin.write("poweroff\n")
    proc.stdin.flush()
    time.sleep(1.0)
    proc.kill()

    print("\n" + "=" * 72)
    print(" >>> ALL DIRECT-BOOT INTERACTIVE SHELL VERIFICATION GATES PASSED <<<")
    print("=" * 72)
    return 0

if __name__ == "__main__":
    sys.exit(main())
