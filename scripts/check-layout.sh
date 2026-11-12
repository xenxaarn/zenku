#!/usr/bin/env bash
# Validate the two-stage El Torito boot image before xorriso embeds it.
#
#   [stage1: 2048 bytes][stage2: N bytes, zero-padded to a 2048 boundary]
set -euo pipefail

IMG=${1:?usage: check-layout.sh <boot-image>}
DEBUG_EXIT=${DEBUG_EXIT:-0}

[ -f "$IMG" ] || { echo "layout: missing $IMG" >&2; exit 1; }

SIZE=$(stat -c%s "$IMG")
if [ "$SIZE" -lt 4096 ]; then
    echo "layout: FAIL: $IMG is $SIZE bytes, expected at least 4096" >&2
    exit 1
fi
if [ $((SIZE % 2048)) -ne 0 ]; then
    echo "layout: FAIL: $IMG is not padded to a 2048-byte boundary ($SIZE)" >&2
    exit 1
fi

# --- stage1 ---------------------------------------------------------------
JMP=$(od -An -v -tx1 -j0 -N2 "$IMG" | tr -d ' \n')
if [ "$JMP" != "eb3e" ]; then
    echo "layout: FAIL: initial jump is 0x$JMP, expected 0xeb3e (entry at 0x7c40)" >&2
    exit 1
fi

SIG_AT_510=$(od -An -v -tx1 -j510 -N2 "$IMG" | tr -d ' \n')
if [ "$SIG_AT_510" = "55aa" ]; then
    echo "layout: note: stage1 still carries the legacy 0x55aa signature" >&2
fi

# The Boot Info Table is patched only inside the ISO; the build artifact keeps
# offsets 8..63 zero.
TABLE=$(od -An -v -tx1 -j8 -N56 "$IMG" | tr -d ' \n')
if [[ ! "$TABLE" =~ ^0+$ ]]; then
    echo "layout: FAIL: Boot Info Table area is not zero" >&2
    echo "               got: $TABLE" >&2
    exit 1
fi

# Entry instruction: `cli` (0xFA), or `mov dx,0x501` (0xBA) in test builds.
ENTRY_OP=$(od -An -v -tx1 -j64 -N1 "$IMG" | tr -d ' \n')
if [ "$DEBUG_EXIT" = "0" ] && [ "$ENTRY_OP" != "fa" ]; then
    echo "layout: FAIL: entry opcode is 0x$ENTRY_OP, expected 0xfa (cli)" >&2
    exit 1
fi
if [ "$DEBUG_EXIT" != "0" ] && [ "$ENTRY_OP" != "fa" ] && [ "$ENTRY_OP" != "ba" ]; then
    echo "layout: FAIL: entry opcode is 0x$ENTRY_OP, expected 0xfa (cli)" >&2
    exit 1
fi

# --- stage2 ---------------------------------------------------------------
MAGIC=$(od -An -v -tx1 -j$((2048 + 4)) -N4 "$IMG" | tr -d ' \n')
if [ "$MAGIC" != "5a535432" ]; then
    echo "layout: FAIL: stage2 magic is 0x$MAGIC, expected 0x5a535432 (\"ZST2\")" >&2
    exit 1
fi

VERSION=$(od -An -v -tu2 -j$((2048 + 8)) -N2 "$IMG" | tr -d ' \n')
if [ "$VERSION" != "1" ]; then
    echo "layout: FAIL: stage2 version is $VERSION, expected 1" >&2
    exit 1
fi

STAGE2_SIZE=$(od -An -v -tu4 -j$((2048 + 12)) -N4 "$IMG" | tr -d ' \n')
if [ $((2048 + STAGE2_SIZE)) -gt "$SIZE" ]; then
    echo "layout: FAIL: stage2 size $STAGE2_SIZE overruns the boot image" >&2
    exit 1
fi

if [ "$DEBUG_EXIT" != "0" ]; then
    echo "layout: OK: $IMG ($SIZE bytes, stage2 $STAGE2_SIZE bytes, test build)"
else
    echo "layout: OK: $IMG ($SIZE bytes, stage2 $STAGE2_SIZE bytes)"
fi
