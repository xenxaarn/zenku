#!/usr/bin/env bash
# Quick headless boot of an ISO with serial capture.
#
#   qemu-headless.sh [iso] [timeout-seconds]
#
# Uses isa-debug-exit so test builds terminate QEMU with status 33. Prints the
# serial log and QEMU status. Always exits 0 unless QEMU itself is missing, so
# it stays useful while the boot chain is incomplete.
set -uo pipefail

ISO=${1:-build/test/os.iso}
TIMEOUT=${2:-20}
QEMU=${QEMU:-qemu-system-x86_64}

[ -f "$ISO" ] || { echo "qemu-headless: missing $ISO" >&2; exit 1; }

DIR=$(dirname "$ISO")
SERIAL="$DIR/serial.log"
LOG="$DIR/qemu.log"
: > "$SERIAL"

timeout "$TIMEOUT" "$QEMU" \
    -cdrom "$ISO" \
    -m 2G \
    -machine pc \
    -display none \
    -serial "file:$SERIAL" \
    -device isa-debug-exit,iobase=0x501,iosize=0x04 \
    -no-reboot \
    > "$LOG" 2>&1
STATUS=$?

echo "--- serial ---"
cat -v "$SERIAL"
echo "--------------"
echo "qemu exit: $STATUS"
if [ "$STATUS" -ne 0 ] && [ "$STATUS" -ne 33 ] && [ "$STATUS" -ne 124 ]; then
    echo "--- qemu log ---"
    cat "$LOG"
fi
exit 0
