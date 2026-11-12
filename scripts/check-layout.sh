#!/usr/bin/env bash
# Validate the raw stage1 boot image before xorriso embeds it.
set -euo pipefail

IMG=${1:?usage: check-layout.sh <stage1.bin>}
EXPECTED_SIZE=2048

[ -f "$IMG" ] || { echo "layout: missing $IMG" >&2; exit 1; }

SIZE=$(stat -c%s "$IMG")
if [ "$SIZE" -ne "$EXPECTED_SIZE" ]; then
    echo "layout: FAIL: $IMG is $SIZE bytes, expected $EXPECTED_SIZE" >&2
    exit 1
fi

SIG=$(od -An -v -tx1 -j510 -N2 "$IMG" | tr -d ' \n')
if [ "$SIG" != "55aa" ]; then
    echo "layout: FAIL: boot signature is 0x$SIG, expected 0x55aa" >&2
    exit 1
fi

# The Boot Info Table is patched only inside the ISO. The build artifact must
# keep offsets 8..63 zero so xorriso has a clean table to fill in.
TABLE=$(od -An -v -tx1 -j8 -N56 "$IMG" | tr -d ' \n')
if [[ ! "$TABLE" =~ ^0+$ ]]; then
    echo "layout: FAIL: Boot Info Table area is not zero" >&2
    echo "               got: $TABLE" >&2
    exit 1
fi

# Entry point must be at offset 64 (0x40), reached by the initial short jump.
JMP=$(od -An -v -tx1 -j0 -N2 "$IMG" | tr -d ' \n')
if [ "$JMP" != "eb3e" ]; then
    echo "layout: FAIL: initial jump is 0x$JMP, expected 0xeb3e (entry at 0x7c40)" >&2
    exit 1
fi

# First instruction at the entry point is `cli` (0xFA).
ENTRY_OP=$(od -An -v -tx1 -j64 -N1 "$IMG" | tr -d ' \n')
if [ "$ENTRY_OP" != "fa" ]; then
    echo "layout: FAIL: entry opcode is 0x$ENTRY_OP, expected 0xfa (cli)" >&2
    exit 1
fi

echo "layout: OK: $IMG ($SIZE bytes, boot signature 0x55aa, entry 0x7c40)"
