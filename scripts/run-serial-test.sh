#!/usr/bin/env bash
# Serial smoke test: boot the ISO, type "help" on COM1 via a socket-backed
# UART, and verify the IRQ4 receive path reached the framebuffer console.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-serial-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-serial.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 20 --wait-line "desktop: terminal ready" \
    --serial-send "help" > "$LOG" 2>&1; then
    echo "run-serial-test: probe failed"
    cat "$LOG"
    exit 1
fi

if ! grep -q "^help" "$LOG"; then
    echo "FAIL: serial text was not echoed by the console"
    tail -20 "$LOG"
    exit 1
fi
if ! grep -q "commands: help, fetch, mem, ticks, uptime, version, clear" "$LOG"; then
    echo "FAIL: serial 'help' command did not run"
    tail -20 "$LOG"
    exit 1
fi

echo "PASS: COM1 input reached the console over IRQ4 and ran 'help'"
