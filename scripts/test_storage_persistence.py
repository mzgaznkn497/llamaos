#!/usr/bin/env python3
"""
LlamaOS/A - Phase D & F Real Storage and Filesystem Persistence Verification Suite
Proves:
1. VirtIO block device discovery on PCI bus
2. Sector bounds checking, multi-region write, flush
3. Guest reboot and exact data persistence (WRITE -> FLUSH -> REBOOT -> READ -> MATCH)
4. Multi-cycle reboot persistence
5. FAT32 persistent filesystem create, write, flush, reboot, read, match
"""

import os
import subprocess
import sys
import time

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT_DIR)

TIMEOUT_SECONDS = 20
TEST_DISK_RAW = "build/testdisk.raw"
TEST_DISK_QCOW2 = "build/testdisk.qcow2"
TEST_FS_RAW = "build/testfs.raw"

def create_temp_iso(cmdline: str, out_iso: str = "build/test_storage.iso"):
    os.makedirs("build/iso_root/boot/grub", exist_ok=True)
    grub_cfg = f"""set timeout=0
set default=0
menuentry "LlamaOS/A Test" {{
    multiboot2 /boot/llamaos.elf mode=test {cmdline}
    boot
}}
"""
    with open("build/iso_root/boot/grub/grub.cfg", "w") as f:
        f.write(grub_cfg)

    subprocess.run(["grub-mkrescue", "-o", out_iso, "build/iso_root"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def run_qemu(iso_path: str, drive_path: str, drive_format: str = "raw", timeout: int = TIMEOUT_SECONDS):
    cmd = [
        "qemu-system-x86_64",
        "-m", "512M",
        "-boot", "d",
        "-cdrom", iso_path,
        "-drive", f"file={drive_path},if=virtio,format={drive_format}",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"
    ]
    start = time.time()
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        text=True
    )
    elapsed = time.time() - start
    return proc.returncode, proc.stdout, elapsed

def main():
    print("=" * 72)
    print(" LlamaOS/A - Real Persistent Storage & Filesystem Verification Suite")
    print("=" * 72)

    os.makedirs("build", exist_ok=True)

    # -------------------------------------------------------------------------
    # PART 1: Raw / QCOW2 Block Storage Persistence Test
    # -------------------------------------------------------------------------
    print("\n[STEP 1] Creating dedicated 64 MiB guest virtual test disk (build/testdisk.raw)...")
    if os.path.exists(TEST_DISK_RAW):
        os.remove(TEST_DISK_RAW)
    with open(TEST_DISK_RAW, "wb") as f:
        f.truncate(64 * 1024 * 1024) # 64 MiB zeroed disk

    # Step 1.1: Write Phase
    print("\n[STEP 2] Launching QEMU Guest - WRITE PHASE (test-storage-write)...")
    create_temp_iso("test-storage-write")
    rc, out, elapsed = run_qemu("build/test_storage.iso", TEST_DISK_RAW, "raw")

    print(f"  Guest finished in {elapsed:.2f}s (Exit code: {rc})")
    assert rc == 33, f"Expected exit code 33, got {rc}. Output:\n{out}"
    assert "[STORAGE_PERSISTENCE_WRITE_PASS]" in out, f"Write pass token missing. Output:\n{out}"
    print("  >>> [CONFIRMED] Multi-region pattern written and flushed to VirtIO disk.")

    # Step 1.2: Host inspects the raw image without guest running
    print("\n[STEP 3] Host-Side Verification of Persistent Virtual Disk Image...")
    with open(TEST_DISK_RAW, "rb") as f:
        # Sector 100 = 100 * 512 = 51200
        f.seek(100 * 512)
        sec100 = f.read(512)
        magic100 = int.from_bytes(sec100[0:8], "little")
        tag100 = sec100[8:38].decode("latin1", errors="ignore")
        print(f"  Host inspected LBA 100: Magic=0x{magic100:016X}, Tag='{tag100}'")
        assert magic100 == 0xAA55BEEF12345678, f"Magic mismatch: 0x{magic100:016X}"
        assert "LLAMAOS_PERSISTENCE_SECTOR_100" in tag100

        # Sector 200
        f.seek(200 * 512)
        sec200 = f.read(512)
        magic200 = int.from_bytes(sec200[0:8], "little")
        print(f"  Host inspected LBA 200: Magic=0x{magic200:016X}")
        assert magic200 == 0xCAFEBABE00000001, f"Magic mismatch: 0x{magic200:016X}"

    print("  >>> [CONFIRMED] Host verified exact data persistence in offline disk image.")

    # Step 1.3: Reboot Phase & Read Verification in a completely new guest process
    print("\n[STEP 4] Powering On Fresh QEMU Guest - READ PHASE (test-storage-read)...")
    create_temp_iso("test-storage-read")
    rc, out, elapsed = run_qemu("build/test_storage.iso", TEST_DISK_RAW, "raw")

    print(f"  Guest finished in {elapsed:.2f}s (Exit code: {rc})")
    assert rc == 33, f"Expected exit code 33, got {rc}. Output:\n{out}"
    assert "[STORAGE_PERSISTENCE_READ_PASS]" in out, f"Read pass token missing. Output:\n{out}"
    print("  >>> [CONFIRMED] WRITE -> FLUSH -> REBOOT -> READ -> EXACT MATCH PROVEN!")

    # Step 1.4: Multi-Cycle Reboot Stability (3 additional reboot cycles)
    print("\n[STEP 5] Testing Multi-Cycle Reboot Verification (3 cycles)...")
    for cycle in range(1, 4):
        rc, out, elapsed = run_qemu("build/test_storage.iso", TEST_DISK_RAW, "raw")
        assert rc == 33 and "[STORAGE_PERSISTENCE_READ_PASS]" in out
        print(f"  Cycle {cycle}/3: Verified exact persistence after reboot ({elapsed:.2f}s)")
    print("  >>> [CONFIRMED] Persistent storage integrity confirmed across repeated reboot cycles.")

    # -------------------------------------------------------------------------
    # PART 2: Real FAT32 Filesystem Persistence Test
    # -------------------------------------------------------------------------
    print("\n[STEP 6] Formatting Dedicated 64 MiB Virtual Disk with FAT32...")
    if os.path.exists(TEST_FS_RAW):
        os.remove(TEST_FS_RAW)
    with open(TEST_FS_RAW, "wb") as f:
        f.truncate(64 * 1024 * 1024) # 64 MiB

    # Use mkfs.vfat on the isolated virtual image file ONLY
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "LLAMAOS_SYS", TEST_FS_RAW],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    print("  >>> FAT32 volume formatted successfully on virtual image.")

    # Step 2.1: Write files inside guest
    print("\n[STEP 7] Launching QEMU Guest - FAT32 CREATE & WRITE PHASE (test-fs-write)...")
    create_temp_iso("test-fs-write")
    rc, out, elapsed = run_qemu("build/test_storage.iso", TEST_FS_RAW, "raw")

    print(f"  Guest finished in {elapsed:.2f}s (Exit code: {rc})")
    assert rc == 33, f"Expected exit code 33, got {rc}. Output:\n{out}"
    assert "[FS_PERSISTENCE_WRITE_PASS]" in out, f"FS write pass token missing. Output:\n{out}"
    print("  >>> [CONFIRMED] Files /persist.txt and /config/sys.txt created and flushed to FAT32.")

    # Step 2.2: Reboot guest and read files back
    print("\n[STEP 8] Powering On Fresh QEMU Guest - FAT32 READ AFTER REBOOT (test-fs-read)...")
    create_temp_iso("test-fs-read")
    rc, out, elapsed = run_qemu("build/test_storage.iso", TEST_FS_RAW, "raw")

    print(f"  Guest finished in {elapsed:.2f}s (Exit code: {rc})")
    assert rc == 33, f"Expected exit code 33, got {rc}. Output:\n{out}"
    assert "[FS_PERSISTENCE_READ_PASS]" in out, f"FS read pass token missing. Output:\n{out}"
    print("  >>> [CONFIRMED] FAT32 FILESYSTEM PERSISTENCE CONFIRMED ACROSS REBOOT!")

    print("\n" + "=" * 72)
    print(" >>> ALL PERSISTENT STORAGE & FILESYSTEM VERIFICATION GATES PASSED <<<")
    print("=" * 72)
    return 0

if __name__ == "__main__":
    sys.exit(main())
