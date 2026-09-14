#!/usr/bin/env python3
"""
Test interactive keyboard input by launching QEMU in normal boot mode,
injecting keystrokes via QEMU monitor socket, and verifying serial echo.
"""

import os
import shutil
import socket
import subprocess
import sys
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT_DIR)

if not os.path.exists("build/iso_root/boot/llamaos.elf") and os.path.exists("build-cmake/llamaos.elf"):
    os.makedirs("build/iso_root/boot", exist_ok=True)
    shutil.copy("build-cmake/llamaos.elf", "build/iso_root/boot/llamaos.elf")

ISO_PATH = "build/test_interactive.iso"
MONITOR_SOCK = "build/qemu_monitor.sock"

# Generate ISO with mode=normal
temp_grub = """set timeout=0
set default=0
menuentry "LlamaOS/A Interactive" {
    multiboot2 /boot/llamaos.elf mode=normal
    boot
}
"""
os.makedirs("build/iso_root/boot/grub", exist_ok=True)
with open("build/iso_root/boot/grub/grub.cfg", "w") as f:
    f.write(temp_grub)
subprocess.run(["grub-mkrescue", "-o", ISO_PATH, "build/iso_root"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

if os.path.exists(MONITOR_SOCK):
    os.remove(MONITOR_SOCK)

cmd = [
    "qemu-system-x86_64",
    "-m", "512M",
    "-cdrom", ISO_PATH,
    "-serial", "stdio",
    "-vnc", ":99",
    "-monitor", f"unix:{MONITOR_SOCK},server,nowait"
]

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

# Wait for socket to be created
time.sleep(1.5)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MONITOR_SOCK)
_ = sock.recv(1024)

def send_qemu_cmd(command):
    sock.sendall((command + "\n").encode())
    time.sleep(0.15)

# Send keystrokes
time.sleep(0.5)
send_qemu_cmd("sendkey h")
send_qemu_cmd("sendkey e")
send_qemu_cmd("sendkey l")
send_qemu_cmd("sendkey l")
send_qemu_cmd("sendkey o")
send_qemu_cmd("sendkey ret")

time.sleep(0.5)
# Quit QEMU
send_qemu_cmd("quit")

stdout, _ = proc.communicate(timeout=5)
sock.close()
if os.path.exists(MONITOR_SOCK):
    os.remove(MONITOR_SOCK)
if os.path.exists(ISO_PATH):
    os.remove(ISO_PATH)
subprocess.run(["make", "iso"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

print("QEMU Output:")
print(stdout)

if "hello" in stdout:
    print("\n[SUCCESS] Interactive keyboard keystrokes 'hello' successfully echoed to console!")
    sys.exit(0)
else:
    print("\n[FAIL] Keystrokes not found in output.")
    sys.exit(1)
