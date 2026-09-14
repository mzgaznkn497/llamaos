# LlamaOS/A — Direct-Boot Installation & Disaster Recovery Runbook

**Document Version:** 1.0.0  
**Target Platform:** DigitalOcean Virtual Private Server (Droplet / KVM)  
**Target Disk:** `/dev/vda` (VirtIO Block Storage)  
**Kernel:** LlamaOS/A x86-64 Enterprise Production Edition  
**Author:** Lead Systems, Storage, Bootloader & Reliability Engineering Team  

---

## 1. Safety Directive & Fundamental Architecture Rule

> [!CAUTION]
> **CRITICAL PRODUCTION SAFETY INVARIANT:**  
> **NEVER** write `llamaos.img` directly to `/dev/vda` from within a live, running Linux host operating system!  
> Overwriting the active root filesystem of a running kernel causes immediate kernel panic, filesystem metadata corruption, unrecoverable data loss, and irrecoverable VM destruction.
>
> All bare-metal flashing procedures MUST be executed strictly from the **DigitalOcean Recovery ISO** (an in-memory live Linux RAMdisk environment where `/dev/vda` is completely unmounted).

---

## 2. Pre-Installation Architecture & Droplet Specification

The direct-boot procedure replaces the stock Linux distribution (e.g., Ubuntu 24.04 LTS) with LlamaOS/A as the bare-metal OS of the virtual machine.

### Target Virtual Hardware Compatibility
- **Virtualization:** KVM / QEMU VirtIO Hardware Virtualization.
- **CPU:** x86-64 Architecture (1 to 32 vCPUs).
- **RAM:** 512 MiB minimum (supports 8+ GiB).
- **Disk Controller:** VirtIO Block Device (`1af4:1001` legacy, `1af4:1042` modern).
- **Network Interface:** VirtIO Network Device (`1af4:1000` legacy, `1af4:1041` modern).
- **Console:** 16550A UART Serial Port (`COM1` at I/O `0x3F8`, 115200 baud, 8-N-1) mapped to DigitalOcean Droplet Web Console / Out-of-Band Serial.

---

## 3. Preparation & Image Verification

Before initiating installation, ensure you have access to the production image artifact and verify its cryptographic integrity against `docs/IMAGE_MANIFEST.txt`.

### Step 1: Verify Host Image Artifact
On your deployment workstation or temporary storage server:

```bash
# Verify file size (must match exactly 268,435,456 bytes)
stat -c "%s %n" build/llamaos.img

# Verify SHA-256 checksum against official release manifest
sha256sum build/llamaos.img
```

Expected Output:
```text
268435456 build/llamaos.img
1da24b4a7bf5b2b7b93014a4020737a6a8757250a2712afd2ed0d1370373420e  build/llamaos.img
```

### Step 2: Stage Image on Web Server or Object Storage
Host `llamaos.img` (or `llamaos.img.gz`) on a secure HTTPS server, DigitalOcean Spaces bucket, or GitHub Releases URL accessible via `curl` or `wget` from the recovery environment.

---

## 4. Step-by-Step Installation Runbook (DigitalOcean Recovery Environment)

### Step 4.1: Boot Droplet into Recovery Environment
1. Log in to the **DigitalOcean Cloud Control Panel** (https://cloud.digitalocean.com).
2. Navigate to **Droplets** -> Select Target Droplet.
3. In the left navigation menu, click **Recovery**.
4. Select **Boot from Recovery ISO**.
5. Click **Power Cycle Droplet**.
6. Navigate to the **Access** tab and launch the **Recovery Console** (or SSH directly to the droplet's public IP address as `root` using your recovery password).

### Step 4.2: Recovery Environment Interactive Menu
Upon connecting to the Recovery ISO console, you will be presented with the DigitalOcean Recovery Menu:

```text
1. Mount your Droplet's filesystem
2. Reset root password
3. Configure networking
4. Check filesystem
5. Test droplet's network
6. Interactive shell
```

Select **Option 6 (Interactive shell)** to enter the bash prompt.

### Step 4.3: Verify Disk Architecture and Ensure Clean State
In the recovery shell:

```bash
# Verify disk layout (ensure /dev/vda is NOT mounted)
lsblk
mount | grep vda

# If any vda partitions were automatically mounted by recovery, unmount them immediately:
umount /dev/vda* 2>/dev/null || true
```

Ensure `/dev/vda` is available and has no active mount points.

### Step 4.4: Stream and Flash LlamaOS/A Disk Image
Download and write `llamaos.img` directly to the raw block device `/dev/vda`:

```bash
# Example 1: Direct streaming over HTTPS with progress and sync
curl -fsSL https://releases.llamaos.org/llamaos-v1.0-production.img.gz | \
  gzip -dc | \
  dd of=/dev/vda bs=4M status=progress conv=fsync

# Example 2: If flashing uncompressed image via curl
curl -fsSL https://releases.llamaos.org/llamaos-v1.0-production.img | \
  dd of=/dev/vda bs=4M status=progress conv=fsync
```

Wait until `dd` finishes and flushes all internal buffers (`conv=fsync`).

### Step 4.5: Validate Partition Table & Filesystem Integrity
Instruct the Linux kernel to re-read the partition table of `/dev/vda`:

```bash
partprobe /dev/vda
sfdisk -l /dev/vda
```

Verify that all three GPT partitions are properly recognized:
- `/dev/vda1`: 4 MiB BIOS Boot Partition (`EF02`)
- `/dev/vda2`: 32 MiB EFI System Partition (`EF00`)
- `/dev/vda3`: 219 MiB FAT32 Root Filesystem (`0700`, Label `LLAMAOS_SYS`)

Run non-destructive integrity checks on the root filesystem:

```bash
fsck.vfat -n /dev/vda3
```

Ensure no filesystem errors or corrupt clusters are reported.

### Step 4.6: Inspect Filesystem Contents
Mount partition 3 read-only to verify kernel binaries and configuration files:

```bash
mkdir -p /mnt/llamaos
mount -o ro /dev/vda3 /mnt/llamaos

ls -la /mnt/llamaos/boot
ls -la /mnt/llamaos/bin
cat /mnt/llamaos/etc/os-release
cat /mnt/llamaos/config/network.cfg

umount /mnt/llamaos
```

### Step 4.7: Configure Static IP & Network Parameters
If your droplet requires a specific static IP configuration (or if not using standard DHCP):

```bash
mount /dev/vda3 /mnt/llamaos

# Edit /mnt/llamaos/config/network.cfg with the droplet's public IP, netmask, and gateway:
cat << 'EOF' > /mnt/llamaos/config/network.cfg
DHCP=no
STATIC_IP=<DROPLET_PUBLIC_IPV4>
NETMASK=255.255.240.0
GATEWAY=<DROPLET_GATEWAY_IPV4>
DNS=8.8.8.8
MGMT_PORT=2222
EOF

umount /mnt/llamaos
sync
```

---

## 5. Booting into LlamaOS/A Bare-Metal

1. Return to the **DigitalOcean Cloud Control Panel**.
2. Navigate to **Recovery** -> Select **Boot from Hard Drive**.
3. In the upper-right corner of the Control Panel, click **Power** -> **Power Cycle**.
4. Immediately navigate to **Access** -> Launch **Droplet Console** (Interactive Serial Console).

### Expected Boot Sequence (Observed via Serial Console)
1. **GRUB 2 Bootloader Screen**:
   - `LlamaOS/A - Direct Boot (Shell)` executes automatically after 1-second timeout.
2. **Kernel Initialization Milestones (Ring 0)**:
   - Early VGA & Unified Serial (COM1 115200 8N1) online.
   - PMM / VMM 4-level paging online.
   - Permanent GDT, TSS, IDT, and exception handlers online.
   - Dual 8259 PIC & PIT Timer @ 100 Hz active.
   - VirtIO-block storage driver discovers `/dev/vda` (256 MiB - 240 GiB).
   - GPT partition parser mounts FAT32 volume `LLAMAOS_SYS` on `/`.
   - VirtIO-net driver configures IP stack and binds TCP Remote Management Console to port 2222.
   - Scheduler transitions CPU to Ring 3 (CPL=3).
3. **Interactive Userland Shell (/bin/sh)**:
   - System displays banner:
     ```text
     ============================================================
      LlamaOS/A Interactive Userland Shell (/bin/sh)
      Running in Ring 3 (CPL=3) | POSIX VFS & Storage Online
     ============================================================
     Type 'help' to view available commands.

     llamaos$ 
     ```

---

## 6. Remote Management Over TCP (Port 2222)

Once LlamaOS/A boots on the droplet, administrators can manage the system remotely over the network via TCP port 2222 without needing the web console:

```bash
# Connect from host / management machine:
telnet <DROPLET_IP> 2222
# or using netcat:
nc <DROPLET_IP> 2222
```

Available Remote Management Commands:
- `help` — Display command menu
- `status` — Real-time system health, architecture, and uptime telemetry
- `mem` — Physical memory manager (PMM) telemetry (total, used, free RAM)
- `ps` — Active process and thread inspection table
- `storage` — Discovered VirtIO block devices and GPT partition table
- `ping` — Network latency verification (returns `PONG`)
- `reboot` — Graceful system restart
- `poweroff` — ACPI powerdown

---

## 7. Disaster Recovery & Emergency Procedures

### Scenario A: Droplet Fails to Boot / Hangs at GRUB Rescue
**Cause:** Corrupted partition table or misconfigured GRUB root.  
**Procedure:**
1. Boot droplet into **DigitalOcean Recovery ISO**.
2. Run `partprobe /dev/vda` and `sfdisk -l /dev/vda`.
3. Check partition 1 (BIOS Boot) and partition 3 (Root).
4. Re-flash image using Section 4.4.

### Scenario B: Filesystem Damage / Unclean Poweroff
**Cause:** Host node sudden reboot or dirty shutdown during heavy write.  
**Procedure:**
1. Boot into Recovery ISO.
2. Run `dosfsck -a -w -v /dev/vda3`.
3. If corruption is unrecoverable, mount `/dev/vda3`, extract critical data files (e.g. `/config/*`), format `/dev/vda3` with `mkfs.vfat -F 32 -n LLAMAOS_SYS /dev/vda3`, and repopulate files from backup.

### Scenario C: Complete VPS Rollback to Ubuntu
If rollback to the default Ubuntu distribution is required:
1. Navigate to **DigitalOcean Cloud Control Panel**.
2. Select **Destroy** -> **Rebuild Droplet**.
3. Select **Ubuntu 24.04 LTS**.
4. Click **Rebuild**. DigitalOcean will re-image the droplet to stock Ubuntu in under 60 seconds.

---
*Verified Production Procedure. Compliant with DigitalOcean Droplet Hardware Architecture.*
