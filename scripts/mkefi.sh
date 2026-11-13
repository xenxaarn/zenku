#!/usr/bin/env bash
# Build the FAT image exposed as the UEFI El Torito boot entry.
#
#   mkefi.sh <BOOTX64.EFI> <rootfs.ext2> <out.img>
#
# The image contains the loader and a copy of the ext2 root filesystem. The
# UEFI loader reads ROOT.EXT2 through SimpleFileSystem, so the ISO can boot
# from the same root filesystem as the BIOS path.
set -euo pipefail

LOADER=${1:?usage: mkefi.sh <BOOTX64.EFI> <rootfs.ext2> <out.img>}
ROOTFS=${2:?usage: mkefi.sh <BOOTX64.EFI> <rootfs.ext2> <out.img>}
OUT=${3:?usage: mkefi.sh <BOOTX64.EFI> <rootfs.ext2> <out.img>}

MKFS=${UEFI_MKFS:-mkfs.fat}
MMD=${UEFI_MMD:-mmd}
MCOPY=${UEFI_MCOPY:-mcopy}

for tool in "$MKFS" "$MMD" "$MCOPY"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "mkefi: missing tool: $tool (install mtools and dosfstools)" >&2
        exit 1
    }
done

[ -f "$LOADER" ] || { echo "mkefi: missing loader: $LOADER" >&2; exit 1; }
[ -f "$ROOTFS" ] || { echo "mkefi: missing rootfs: $ROOTFS" >&2; exit 1; }

# Leave room for FAT metadata and future loader changes. FAT16 keeps the
# El Torito image compact while comfortably fitting ROOT.EXT2.
size_kib=$(( $(stat -c%s "$ROOTFS") / 1024 + 4096 ))
size_kib=$(( (size_kib + 1023) / 1024 * 1024 ))

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
"$MKFS" -C -F 16 -n ZENKUEFI "$OUT" "$size_kib" >/dev/null

"$MMD" -i "$OUT" ::/EFI ::/EFI/BOOT
"$MCOPY" -i "$OUT" "$LOADER" ::/EFI/BOOT/BOOTX64.EFI
"$MCOPY" -i "$OUT" "$ROOTFS" ::/ROOT.EXT2

echo "mkefi: wrote $OUT ($size_kib KiB)"
