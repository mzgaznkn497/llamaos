#!/usr/bin/env bash
# =============================================================================
# LlamaOS/A - Bootable Persistent Disk Image Generator (Phase H)
# =============================================================================
# Generates build/llamaos.img:
# - Pure GPT partitioned raw disk image (256 MiB)
# - Partition 1: BIOS Boot Partition (4 MiB, GPT GUID 21686148-6449-6E6F-744E-656564454649)
# - Partition 2: EFI System Partition (32 MiB, FAT32, GPT GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B)
# - Partition 3: LlamaOS/A Root System Partition (FAT32, label LLAMAOS_ROOT)
# - Installs GRUB 2 for both BIOS (i386-pc) and UEFI (x86_64-efi)
# - Populates /boot/llamaos.elf, /boot/grub/grub.cfg, /bin/init, /bin/sh, and config files
# =============================================================================

set -euo pipefail

DISK_IMAGE="${1:-build/llamaos.img}"
DISK_SIZE_MIB=256
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

mkdir -p build

echo "================================================================================"
echo " LlamaOS/A - Generating Bootable Persistent Disk Image: $DISK_IMAGE"
echo "================================================================================"

# Verify prerequisites
for tool in sgdisk losetup mkfs.vfat grub-install; do
    if ! command -v "$tool" &>/dev/null; then
        echo "[ERROR] Required tool '$tool' not found!" >&2
        exit 1
    fi
done

if [[ ! -f "build/llamaos.elf" ]]; then
    echo "[ERROR] Kernel ELF 'build/llamaos.elf' not found. Run 'make kernel' first." >&2
    exit 1
fi

MNT_DIR=$(mktemp -d /tmp/llamaos_mnt_XXXXXX)
LOOPDEV=""

cleanup() {
    echo "[INFO] Cleaning up mount points and loop devices..."
    set +e
    if [[ -d "$MNT_DIR/efi" ]]; then
        umount "$MNT_DIR/efi" 2>/dev/null || true
    fi
    if [[ -d "$MNT_DIR" ]]; then
        umount "$MNT_DIR" 2>/dev/null || true
        rm -rf "$MNT_DIR"
    fi
    if [[ -n "$LOOPDEV" ]]; then
        losetup -d "$LOOPDEV" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "[1/6] Creating ${DISK_SIZE_MIB} MiB zeroed disk image..."
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count="$DISK_SIZE_MIB" status=none

echo "[2/6] Writing GPT partition table (BIOS Boot, ESP, Root)..."
sgdisk -Z "$DISK_IMAGE" >/dev/null 2>&1
sgdisk -o "$DISK_IMAGE" >/dev/null 2>&1
# Partition 1: BIOS Boot Partition (4 MiB)
sgdisk -n 1:2048:10239 -t 1:ef02 -c 1:"BIOS_BOOT" "$DISK_IMAGE" >/dev/null 2>&1
# Partition 2: EFI System Partition (32 MiB)
sgdisk -n 2:10240:75775 -t 2:ef00 -c 2:"ESP" "$DISK_IMAGE" >/dev/null 2>&1
# Partition 3: LlamaOS/A Root System Partition (Remainder ~220 MiB)
sgdisk -n 3:75776:0 -t 3:0700 -c 3:"LLAMAOS_ROOT" "$DISK_IMAGE" >/dev/null 2>&1

echo "[3/6] Formatting partitions with FAT32..."
LOOPDEV=$(losetup -Pf --show "$DISK_IMAGE")
mkfs.vfat -F 32 -n "ESP" "${LOOPDEV}p2" >/dev/null 2>&1
mkfs.vfat -F 32 -n "LLAMAOS_SYS" "${LOOPDEV}p3" >/dev/null 2>&1

echo "[4/6] Mounting partitions..."
mount "${LOOPDEV}p3" "$MNT_DIR"
mkdir -p "$MNT_DIR/boot/grub" "$MNT_DIR/efi" "$MNT_DIR/bin" "$MNT_DIR/etc" "$MNT_DIR/config"
mount "${LOOPDEV}p2" "$MNT_DIR/efi"

echo "[5/6] Installing Dual-Boot GRUB 2 (BIOS i386-pc & UEFI x86_64-efi)..."
grub-install --target=i386-pc --boot-directory="$MNT_DIR/boot" "$LOOPDEV" >/dev/null 2>&1
grub-install --target=x86_64-efi --boot-directory="$MNT_DIR/boot" --efi-directory="$MNT_DIR/efi" --removable >/dev/null 2>&1

echo "[6/6] Populating persistent filesystem (/boot, /bin, /etc, /config)..."
cp build/llamaos.elf "$MNT_DIR/boot/llamaos.elf"

cat > "$MNT_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=1
set default=0

insmod fat
insmod part_gpt

menuentry "LlamaOS/A - Direct Boot (Shell)" {
    multiboot2 /boot/llamaos.elf mode=shell
    boot
}

menuentry "LlamaOS/A - Direct Boot (Self-Test)" {
    multiboot2 /boot/llamaos.elf mode=test
    boot
}
EOF

# Install userland binaries
if [[ -f "build/init.elf" ]]; then
    cp build/init.elf "$MNT_DIR/bin/init"
fi

if [[ -f "build/sh.elf" ]]; then
    cp build/sh.elf "$MNT_DIR/bin/sh"
fi

# Configuration and identification files
cat > "$MNT_DIR/etc/os-release" << 'EOF'
NAME="LlamaOS/A"
VERSION="1.0-production"
ID=llamaos
PRETTY_NAME="LlamaOS/A Direct-Boot VPS Enterprise Edition"
EOF

cat > "$MNT_DIR/etc/hostname" << 'EOF'
llamaos-vps
EOF

cat > "$MNT_DIR/config/network.cfg" << 'EOF'
DHCP=yes
STATIC_IP=10.0.2.15
NETMASK=255.255.255.0
GATEWAY=10.0.2.2
DNS=8.8.8.8
MGMT_PORT=2222
EOF

sync

echo "================================================================================"
echo " >>> SUCCESS: Bootable persistent disk image ready: $DISK_IMAGE <<<"
echo "================================================================================"
