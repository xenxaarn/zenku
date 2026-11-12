#!/usr/bin/env bash
# Assemble the two-stage boot image.
#
#   build-boot.sh <stage1.asm> <stage2.bin> <out-boot-image> [nasm-flags]
#
# The output starts with a 2048-byte stage1, followed by the raw stage2
# payload, padded to a 2048-byte boundary. Stage1 receives STAGE2_BYTES so it
# knows exactly how much to read from the El Torito extent.
#
# The optional fourth argument is passed to both NASM invocations, e.g.
# "-dTEST_EXIT" for the headless integration test build.
set -euo pipefail

STAGE1_ASM=${1:?usage: build-boot.sh <stage1.asm> <stage2.bin> <out> [nasm-flags]}
STAGE2_BIN=${2:?usage: build-boot.sh <stage1.asm> <stage2.bin> <out> [nasm-flags]}
OUT=${3:?usage: build-boot.sh <stage1.asm> <stage2.bin> <out> [nasm-flags]}
EXTRA_FLAGS=${4:-}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

[ -f "$STAGE1_ASM" ] || { echo "build-boot: missing $STAGE1_ASM" >&2; exit 1; }
[ -f "$STAGE2_BIN" ] || { echo "build-boot: missing $STAGE2_BIN" >&2; exit 1; }

SIZE=$(stat -c%s "$STAGE2_BIN")
if [ "$SIZE" -le 0 ] || [ "$SIZE" -gt 65536 ]; then
    echo "build-boot: stage2 must be 1..65536 bytes (got $SIZE)" >&2
    exit 1
fi

TMP=$(mktemp -d "$ROOT/build/bootimg.XXXXXX")
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

NASM=${NASM:-nasm}
# shellcheck disable=SC2086
"$NASM" -f bin -I"$ROOT" -DSTAGE2_BYTES="$SIZE" $EXTRA_FLAGS \
    -o "$TMP/stage1.bin" "$STAGE1_ASM"

STAGE1_SIZE=$(stat -c%s "$TMP/stage1.bin")
if [ "$STAGE1_SIZE" -ne 2048 ]; then
    echo "build-boot: stage1 is $STAGE1_SIZE bytes, expected 2048" >&2
    exit 1
fi

cp "$STAGE2_BIN" "$TMP/stage2.bin"

# Patch the stage2 size field at offset 12 (little endian).
le32() {
    local v=$1
    printf "\\$(printf '%03o' $((v & 255)))"
    printf "\\$(printf '%03o' $(((v >> 8) & 255)))"
    printf "\\$(printf '%03o' $(((v >> 16) & 255)))"
    printf "\\$(printf '%03o' $(((v >> 24) & 255)))"
}
le32 "$SIZE" | dd of="$TMP/stage2.bin" bs=1 seek=12 conv=notrunc status=none

mkdir -p "$(dirname "$OUT")"
cat "$TMP/stage1.bin" "$TMP/stage2.bin" > "$OUT"

TOTAL=$(stat -c%s "$OUT")
PAD=$(( (2048 - (TOTAL % 2048)) % 2048 ))
if [ "$PAD" -ne 0 ]; then
    truncate -s $((TOTAL + PAD)) "$OUT"
fi

echo "build-boot: stage2 $SIZE bytes -> $(stat -c%s "$OUT") byte boot image"
