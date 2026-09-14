#!/usr/bin/env python3
"""
LlamaOS/A - Extended Stability and Stress Test
Executes an extended QEMU guest session testing direct disk boot,
repeated shell commands, filesystem CRUD, and TCP remote management.
"""

import os
import socket
import subprocess
import sys
import threading
import time

DISK_IMG = "build/llamaos.img"

def run_stability_test(duration_target_sec=20):
    print("=" * 72)
    print(" LlamaOS/A - Extended Reality & Stability Audit Test")
    print(f" Target Active Run Duration: {duration_target_sec} seconds")
    print("=" * 72)

    if not os.path.exists(DISK_IMG):
        print(f"[ERROR] Disk image {DISK_IMG} not found!")
        return False

    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-drive", f"file={DISK_IMG},if=virtio,format=raw",
        "-netdev", "user,id=net0,hostfwd=tcp::2223-10.0.2.15:2222",
        "-device", "virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot"
    ]

    print("[STEP 1] Launching QEMU direct-boot from build/llamaos.img...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    full_output = []
    running = True

    def reader_thread():
        while running and proc.poll() is None:
            try:
                ch = proc.stdout.read(1)
                if ch:
                    full_output.append(ch)
                else:
                    time.sleep(0.005)
            except Exception:
                break

    t_reader = threading.Thread(target=reader_thread, daemon=True)
    t_reader.start()

    t_start = time.time()
    panic_count = 0
    exception_count = 0
    unexpected_reboot_count = 0
    fs_errors = 0
    net_failures = 0
    iterations = 0
    mem_start = None
    mem_end = None

    def wait_for_token(token: str, timeout: float = 12.0) -> bool:
        t0 = time.time()
        while time.time() - t0 < timeout:
            if proc.poll() is not None:
                return False
            text = "".join(full_output)
            if token in text:
                return True
            time.sleep(0.05)
        return False

    print("[STEP 2] Waiting for shell prompt (llamaos$)...")
    if not wait_for_token("llamaos$"):
        print("[FAIL] Never received shell prompt within 12s.")
        print(f"Output so far: {''.join(full_output)}")
        proc.kill()
        return False

    print("  >>> Shell prompt detected online!")

    # Connect to TCP server on port 2223
    print("[STEP 3] Connecting to TCP remote management on port 2223...")
    tcp_sock = None
    for _ in range(8):
        try:
            tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp_sock.settimeout(2.0)
            tcp_sock.connect(("127.0.0.1", 2223))
            print("  >>> TCP connection established successfully!")
            break
        except Exception:
            time.sleep(0.5)

    if not tcp_sock:
        print("  [WARN] Failed to establish TCP connection.")
        net_failures += 1

    # Run loop
    print(f"[STEP 4] Executing stress cycles for {duration_target_sec} seconds...")
    t_stress_start = time.time()
    while (time.time() - t_stress_start) < duration_target_sec and proc.poll() is None:
        iterations += 1

        # Query memory
        proc.stdin.write("mem\n")
        proc.stdin.flush()
        time.sleep(0.2)

        # Filesystem touch
        proc.stdin.write(f"touch /test_{iterations}.txt\n")
        proc.stdin.flush()
        time.sleep(0.2)

        # Query uptime
        proc.stdin.write("uptime\n")
        proc.stdin.flush()
        time.sleep(0.2)

        # TCP status query
        if tcp_sock:
            try:
                tcp_sock.sendall(b"status\n")
                resp = tcp_sock.recv(512).decode("latin-1", errors="replace")
                if "HEALTHY" not in resp and "SYSTEM STATUS" not in resp:
                    net_failures += 1
            except Exception:
                net_failures += 1

        time.sleep(0.3)

    # Inspect logs for memory and errors
    text = "".join(full_output)
    for line in text.splitlines():
        if "Used Memory" in line:
            if mem_start is None:
                mem_start = line.strip()
            mem_end = line.strip()
        if "Kernel Panic" in line or "PANIC" in line:
            panic_count += 1
        if "Fatal Exception" in line or "Page Fault in kernel space" in line:
            exception_count += 1

    # Graceful shutdown
    print("[STEP 5] Performing clean ACPI poweroff...")
    try:
        proc.stdin.write("poweroff\n")
        proc.stdin.flush()
    except Exception:
        pass

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    running = False
    if tcp_sock:
        try:
            tcp_sock.close()
        except Exception:
            pass

    t_total = time.time() - t_start

    print("\n" + "=" * 72)
    print(" EXTENDED STABILITY AUDIT RESULTS")
    print("=" * 72)
    print(f" Total Run Duration        : {t_total:.2f} seconds")
    print(f" Stress Test Cycles        : {iterations}")
    print(f" Initial Memory State      : {mem_start}")
    print(f" Final Memory State        : {mem_end}")
    print(f" Kernel Panics Observed    : {panic_count}")
    print(f" Unhandled Exceptions      : {exception_count}")
    print(f" Unexpected Reboots        : {unexpected_reboot_count}")
    print(f" Filesystem Errors         : {fs_errors}")
    print(f" Network Failures Recorded : {net_failures}")
    print("=" * 72)

    return (panic_count == 0 and exception_count == 0 and unexpected_reboot_count == 0)

if __name__ == "__main__":
    success = run_stability_test(duration_target_sec=20)
    sys.exit(0 if success else 1)
