#!/usr/bin/env bash
# Interactive smoke test: boot the ISO, type "help" through the QEMU monitor,
# and verify that the PS/2 keyboard path reached the framebuffer console.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-input-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-input.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 8 --keys "h,e,l,p,ret" > "$LOG" 2>&1; then
    echo "run-input-test: probe failed"
    cat "$LOG"
    exit 1
fi

if ! grep -q "^help" "$LOG"; then
    echo "FAIL: typed text did not reach the console"
    tail -20 "$LOG"
    exit 1
fi
if ! grep -q "commands: help, mem, ticks, version, clear" "$LOG"; then
    echo "FAIL: help command did not run"
    tail -20 "$LOG"
    exit 1
fi

echo "PASS: PS/2 keyboard input reached the console and ran 'help'"
