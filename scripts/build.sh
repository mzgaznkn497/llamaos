#!/usr/bin/env bash
# ==============================================================================
# LlamaOS/A - Build Entry Point
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Building LlamaOS/A Kernel and Bootable Media ==="
cd "${ROOT_DIR}"
make -j"$(nproc)"
echo "=== Build Complete ==="
