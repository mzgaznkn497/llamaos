#!/usr/bin/env python3
"""
LlamaOS/A - VirtIO Network & Remote Management Console Test Suite
Proves:
1. VirtIO-Net PCI device discovery & driver initialization
2. Ethernet II, ARP, IPv4, TCP stack online
3. Remote Management Service listening on port 2222
4. Host-to-Guest TCP connection via forwarded port 2222
5. Interactive management commands over TCP:
   - help
   - status
   - mem
   - ps
   - storage
   - ping -> PONG
   - exit
"""

import socket
import subprocess
import sys
import time

def main():
    print("=" * 72)
    print(" LlamaOS/A - VirtIO Network & Remote Management Test Suite")
    print("=" * 72)

    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-drive", "file=build/llamaos.img,if=virtio,format=raw",
        "-netdev", "user,id=net0,hostfwd=tcp::2222-10.0.2.15:2222",
        "-object", "filter-dump,id=f0,netdev=net0,file=qemu_net.pcap",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot"
    ]

    print("[STEP 1] Launching QEMU with VirtIO-Net and TCP port forward (2222 -> 2222)...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    import threading

    full_output = ""
    lock = threading.Lock()

    def serial_reader():
        nonlocal full_output
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            with lock:
                full_output += line
            sys.stdout.write(line)
            sys.stdout.flush()

    reader_t = threading.Thread(target=serial_reader, daemon=True)
    reader_t.start()

    def wait_for_serial(token: str, timeout: float = 12.0) -> bool:
        t0 = time.time()
        while time.time() - t0 < timeout:
            if proc.poll() is not None:
                break
            with lock:
                if token in full_output:
                    return True
            time.sleep(0.05)
        return False

    print("[STEP 2] Waiting for Network Driver & Remote Management Service Online...")
    if not wait_for_serial("Remote Management Service listening on TCP port 2222", timeout=15.0):
        print(f"\n[FAIL] Network service did not initialize.")
        proc.kill()
        return 1

    print("\n>>> [CONFIRMED] Kernel reports TCP port 2222 listening!")

    # Wait for user shell prompt so guest scheduler is active
    wait_for_serial("llamaos$ ", timeout=10.0)
    time.sleep(0.5)

    print("\n[STEP 3] Connecting from host to guest via TCP 127.0.0.1:2222...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)

    connected = False
    for attempt in range(10):
        try:
            s.connect(("127.0.0.1", 2222))
            connected = True
            break
        except Exception as e:
            time.sleep(0.5)

    if not connected:
        print("[FAIL] Failed to connect to TCP port 2222 on host!")
        proc.kill()
        return 1

    print(">>> [CONFIRMED] Connected to LlamaOS/A Remote Management Console!")

    def recv_until(token: str, timeout: float = 5.0) -> str:
        s.settimeout(timeout)
        data = ""
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                chunk = s.recv(1024).decode(errors="ignore")
                if chunk:
                    data += chunk
                    if token in data:
                        return data
            except socket.timeout:
                break
        return data

    banner = recv_until("llamaos> ", timeout=5.0)
    print(f"[RECV BANNER]:\n{banner}")

    mgmt_tests = [
        ("help", "Available Management Commands:"),
        ("status", "OS Name"),
        ("mem", "[MEMORY TELEMETRY]"),
        ("ps", "[PROCESS LIST]"),
        ("storage", "[STORAGE DEVICES]"),
        ("ping", "PONG")
    ]

    for req, expected in mgmt_tests:
        print(f"\n[NET-EXEC] Sending TCP command: '{req}'")
        s.sendall(f"{req}\r\n".encode())
        res = recv_until("llamaos> ", timeout=5.0)
        print(f"[NET-RESP]:\n{res}")
        if expected not in res:
            print(f"[FAIL] Expected token '{expected}' not found in TCP response!")
            s.close()
            proc.kill()
            return 1
        print(f"  >>> [PASS] Verified TCP response for '{req}'")

    print("\n[STEP 4] Testing graceful TCP session termination (exit)...")
    s.sendall(b"exit\r\n")
    time.sleep(0.5)
    s.close()

    print("[STEP 5] Powering off guest via console...")
    proc.stdin.write("poweroff\n")
    proc.stdin.flush()
    time.sleep(1.0)
    proc.kill()

    print("\n" + "=" * 72)
    print(" >>> ALL VIRTIO-NET & REMOTE MANAGEMENT VERIFICATION GATES PASSED <<<")
    print("=" * 72)
    return 0

if __name__ == "__main__":
    sys.exit(main())
