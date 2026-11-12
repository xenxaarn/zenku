#!/usr/bin/env bash
# Headless Phase 0 integration test.
#
# The test ISO is built with -dTEST_EXIT, so stage1 writes 0x10 to the
# isa-debug-exit port. QEMU then exits with status (0x10 << 1) | 1 == 33.
set -uo pipefail

ISO=${1:-build/test/os.iso}
[ -f "$ISO" ] || { echo "run-tests: missing ISO: $ISO" >&2; exit 1; }

QEMU=${QEMU:-qemu-system-x86_64}
TEST_DIR=$(dirname "$ISO")
SERIAL_LOG="$TEST_DIR/serial.log"
QEMU_LOG="$TEST_DIR/qemu.log"
EXPECTED_EXIT=33
TIMEOUT=${TEST_TIMEOUT:-20}

: > "$SERIAL_LOG"

set +e
timeout "$TIMEOUT" "$QEMU" \
    -cdrom "$ISO" \
    -m 512M \
    -machine pc \
    -display none \
    -serial "file:$SERIAL_LOG" \
    -device isa-debug-exit,iobase=0x501,iosize=0x04 \
    -no-reboot \
    >"$QEMU_LOG" 2>&1
STATUS=$?
set -e

echo "--- serial ---"
cat "$SERIAL_LOG" 2>/dev/null || true
echo "--------------"
echo "qemu exit status: $STATUS (expected $EXPECTED_EXIT)"

if [ "$STATUS" -ne "$EXPECTED_EXIT" ]; then
    echo "FAIL: unexpected QEMU exit status"
    echo "--- qemu log ---"
    cat "$QEMU_LOG" 2>/dev/null || true
    exit 1
fi

if ! grep -q "ZENKU STAGE1 OK" "$SERIAL_LOG"; then
    echo "FAIL: stage1 banner missing from serial output"
    exit 1
fi

if ! grep -q "boot drive: 0x" "$SERIAL_LOG"; then
    echo "FAIL: boot drive line missing from serial output"
    exit 1
fi

echo "PASS: stage1 booted and reported over COM1"
