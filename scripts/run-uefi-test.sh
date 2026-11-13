#!/usr/bin/env bash
# UEFI smoke test: boot the ISO with OVMF, let the firmware load BOOTX64.EFI,
# and verify the loader reaches the same kernel and userland as the BIOS path.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-uefi-test: missing ISO: $ISO" >&2; exit 1; }

find_ovmf() {
    local candidate

    for candidate in \
        "${OVMF_CODE:-}" \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/ovmf/OVMF_CODE.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/share/edk2/x64/OVMF_CODE.fd; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

if ! OVMF=$(find_ovmf); then
    echo "run-uefi-test: OVMF_CODE.fd not found (install ovmf, or set OVMF_CODE)" >&2
    exit 1
fi

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-uefi.XXXXXX")
CLEAN=$(mktemp "${TMPDIR:-/tmp}/zenku-uefi-clean.XXXXXX")
trap 'rm -f "$LOG" "$CLEAN"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" --bios "$OVMF" \
    --wait 90 --wait-line "user: /init returned to the kernel" \
    --serial-send "help" > "$LOG" 2>&1; then
    echo "run-uefi-test: probe failed"
    tail -80 "$LOG"
    exit 1
fi

tr -d '\r' < "$LOG" > "$CLEAN"

check() {
    if ! grep -q -- "$1" "$CLEAN"; then
        echo "FAIL: missing: $1"
        tail -80 "$LOG"
        exit 1
    fi
}

check "ZENKU UEFI OK"
check "uefi: ROOT.EXT2 loaded"
check "uefi: e820 "
check "ZENKU KERNEL OK"
check "kernel: firmware UEFI"
check "desktop: terminal ready"
check "commands: help"
check "user: /init returned to the kernel"

echo "PASS: OVMF loaded BOOTX64.EFI and booted the Thunder kernel and userland"
