#!/usr/bin/env bash
# ==============================================================================
# LlamaOS/A - QEMU Launch Helper Script
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ISO_IMAGE="${ROOT_DIR}/build/llamaos.iso"

OVMF_PATH="/usr/share/ovmf/OVMF.fd"
if [[ ! -f "${OVMF_PATH}" ]]; then
    OVMF_PATH="/usr/share/OVMF/OVMF_CODE_4M.fd"
fi

UEFI_MODE=0
DEBUG_MODE=0
TEST_MODE=0

for arg in "$@"; do
    case "$arg" in
        --uefi)
            UEFI_MODE=1
            ;;
        --debug)
            DEBUG_MODE=1
            ;;
        --test)
            TEST_MODE=1
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--uefi] [--debug] [--test]"
            exit 1
            ;;
    esac
done

if [[ ! -f "${ISO_IMAGE}" ]]; then
    echo "ISO image not found at ${ISO_IMAGE}. Building first..."
    make -C "${ROOT_DIR}" iso
fi

QEMU_ARGS=(
    -m 512M
    -cdrom "${ISO_IMAGE}"
    -serial stdio
    -vga std
)

if [[ ${UEFI_MODE} -eq 1 ]]; then
    echo "Running with UEFI firmware (${OVMF_PATH})..."
    QEMU_ARGS+=(-bios "${OVMF_PATH}")
fi

if [[ ${DEBUG_MODE} -eq 1 ]]; then
    echo "Running in GDB debug mode on port 1234 (-s -S)..."
    QEMU_ARGS+=(-s -S)
fi

if [[ ${TEST_MODE} -eq 1 ]]; then
    echo "Running with ISA debug exit enabled..."
    QEMU_ARGS+=(-device isa-debug-exit,iobase=0xf4,iosize=0x04)
fi

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
