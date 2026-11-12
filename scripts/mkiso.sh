#!/usr/bin/env bash
# Build a bootable El Torito ISO around a stage1 boot image.
set -euo pipefail

BOOT_IMAGE=${1:?usage: mkiso.sh <stage1.bin> <out.iso>}
OUT=${2:?usage: mkiso.sh <stage1.bin> <out.iso>}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LABEL=${ISO_LABEL:-ZENKU}
XORRISO=${XORRISO:-xorriso}

if [ ! -f "$BOOT_IMAGE" ]; then
    echo "mkiso: missing boot image: $BOOT_IMAGE" >&2
    exit 1
fi

mkdir -p "$ROOT/build"
STAGE=$(mktemp -d "$ROOT/build/iso.XXXXXX")
cleanup() {
    rm -rf "$STAGE"
}
trap cleanup EXIT

mkdir -p "$STAGE/boot"
cp "$BOOT_IMAGE" "$STAGE/boot/stage1.bin"

cat > "$STAGE/README.TXT" <<'EOF'
Zenku - 64-bit Unix-like operating system
Phase 0 boot media

This disc uses an El Torito no-emulation boot image at boot/stage1.bin.
EOF

mkdir -p "$(dirname "$OUT")"

"$XORRISO" -as mkisofs \
    -r -J \
    -V "$LABEL" \
    -b boot/stage1.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -o "$OUT" \
    "$STAGE"

echo "mkiso: wrote $OUT"
