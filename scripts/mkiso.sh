#!/usr/bin/env bash
# Build a bootable El Torito ISO.
#
#   mkiso.sh <boot-image> <out.iso> [stage3.bin] [root.ext2] [efi.img]
#
# STAGE3.BIN and ROOT.EXT2 are placed in the ISO root. When they are not
# supplied, small placeholders are generated so the boot-chain milestones can
# be tested before the loader and root filesystem exist. EFI.IMG, when given,
# becomes a second El Torito entry for UEFI firmware.
set -euo pipefail

BOOT_IMAGE=${1:?usage: mkiso.sh <boot-image> <out.iso> [stage3.bin] [root.ext2]}
OUT=${2:?usage: mkiso.sh <boot-image> <out.iso> [stage3.bin] [root.ext2]}
STAGE3=${3:-}
ROOTFS=${4:-}
EFIIMG=${5:-}

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

if [ -n "$STAGE3" ] && [ -f "$STAGE3" ]; then
    cp "$STAGE3" "$STAGE/STAGE3.BIN"
else
    {
        printf 'ZENKU STAGE3 PLACEHOLDER\n'
        printf 'Replace with the real 64-bit stage3 payload.\n'
    } > "$STAGE/STAGE3.BIN"
    truncate -s 4096 "$STAGE/STAGE3.BIN"
fi

if [ -n "$ROOTFS" ] && [ -f "$ROOTFS" ]; then
    cp "$ROOTFS" "$STAGE/ROOT.EXT2"
else
    printf 'ZENKU ROOT.EXT2 PLACEHOLDER\n' > "$STAGE/ROOT.EXT2"
    truncate -s 1048576 "$STAGE/ROOT.EXT2"
fi

if [ -n "$EFIIMG" ] && [ -f "$EFIIMG" ]; then
    cp "$EFIIMG" "$STAGE/efi.img"
fi

cat > "$STAGE/README.TXT" <<'EOF'
Zenku - 64-bit Unix-like operating system

This disc boots from either the El Torito BIOS image at boot/stage1.bin or
the UEFI FAT image at efi.img, depending on the firmware.
EOF

mkdir -p "$(dirname "$OUT")"

if [ -n "$EFIIMG" ] && [ -f "$EFIIMG" ]; then
    "$XORRISO" -as mkisofs \
        -r -J -iso-level 2 \
        -V "$LABEL" \
        -b boot/stage1.bin \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        -eltorito-alt-boot \
        -e efi.img \
        -no-emul-boot \
        -o "$OUT" \
        "$STAGE"
else
    "$XORRISO" -as mkisofs \
        -r -J -iso-level 2 \
        -V "$LABEL" \
        -b boot/stage1.bin \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        -o "$OUT" \
        "$STAGE"
fi

echo "mkiso: wrote $OUT"
