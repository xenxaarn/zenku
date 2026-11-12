#!/usr/bin/env bash
# Run one fault-injection kernel and assert that the exception produced a
# readable panic. QEMU exits 65 because kpanic() writes 0x20.
#
#   run-fault-test.sh <iso> <vector>
#
# vector: 0 #DE, 1 #UD, 2 #GP, 3 #PF
set -uo pipefail

ISO=${1:?usage: run-fault-test.sh <iso> <vector>}
VECTOR=${2:?usage: run-fault-test.sh <iso> <vector>}
[ -f "$ISO" ] || { echo "run-fault-test: missing ISO: $ISO" >&2; exit 1; }

case "$VECTOR" in
0) EXPECT="EXCEPTION 0: divide error" ;;
1) EXPECT="EXCEPTION 6: invalid opcode" ;;
2) EXPECT="EXCEPTION 13: general protection fault" ;;
3) EXPECT="EXCEPTION 14: page fault" ;;
*)
    echo "run-fault-test: unknown vector: $VECTOR" >&2
    exit 2
    ;;
esac

QEMU=${QEMU:-qemu-system-x86_64}
TEST_DIR=$(dirname "$ISO")
SERIAL_LOG="$TEST_DIR/serial.log"
QEMU_LOG="$TEST_DIR/qemu.log"
EXPECTED_EXIT=65
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

echo "--- fault test vector $VECTOR ---"
grep -E "triggering|EXCEPTION|rip:|cr2:|PANIC" "$SERIAL_LOG" || true
echo "qemu exit status: $STATUS (expected $EXPECTED_EXIT)"

if [ "$STATUS" -ne "$EXPECTED_EXIT" ]; then
    echo "FAIL: vector $VECTOR did not panic cleanly"
    cat "$SERIAL_LOG" 2>/dev/null || true
    exit 1
fi

require() {
    if ! grep -q "$1" "$SERIAL_LOG"; then
        echo "FAIL: vector $VECTOR: missing: $1"
        exit 1
    fi
}

require "$EXPECT"
require "PANIC: unhandled exception"
if [ "$VECTOR" -eq 3 ]; then
    require "cr2: 0xffffffffb0000000"
fi

echo "PASS: vector $VECTOR produced a readable panic"
