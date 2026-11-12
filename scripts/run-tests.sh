#!/usr/bin/env bash
# Headless integration test for the boot chain.
#
# The test ISO is built with -dTEST_EXIT. QEMU is started with isa-debug-exit,
# so the last stage that reaches the test hook writes 0x10 and QEMU exits with
# status (0x10 << 1) | 1 == 33.
set -uo pipefail

ISO=${1:-build/test/os.iso}
[ -f "$ISO" ] || { echo "run-tests: missing ISO: $ISO" >&2; exit 1; }

QEMU=${QEMU:-qemu-system-x86_64}
TEST_DIR=$(dirname "$ISO")
SERIAL_LOG="$TEST_DIR/serial.log"
QEMU_LOG="$TEST_DIR/qemu.log"
EXPECTED_EXIT=33
TIMEOUT=${TEST_TIMEOUT:-30}

: > "$SERIAL_LOG"

set +e
timeout "$TIMEOUT" "$QEMU" \
    -cdrom "$ISO" \
    -m 2G \
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

require() {
    if ! grep -q "$1" "$SERIAL_LOG"; then
        echo "FAIL: missing serial line: $1"
        exit 1
    fi
}

require "ZENKU STAGE1 OK"
require "boot drive: 0x"
require "ZENKU STAGE2 OK"

echo "PASS: boot chain reached stage2"
