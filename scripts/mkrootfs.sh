#!/usr/bin/env bash
# Build the read-only ext2 root filesystem embedded in the ISO.
#
#   mkrootfs.sh <kernel.elf> <out.ext2> [size-mib]
#
# The image is populated from a staging directory, so it contains /kernel and
# a short README. Keep the feature set small: the Zenku ext2 reader supports
# 1/2/4 KiB blocks and rejects 64BIT, EXTENTS, INLINE_DATA and META_BG.
set -euo pipefail

KERNEL=${1:?usage: mkrootfs.sh <kernel.elf> <out.ext2> [size-mib]}
OUT=${2:?usage: mkrootfs.sh <kernel.elf> <out.ext2> [size-mib]}
SIZE_MIB=${3:-32}
MKE2FS=${MKE2FS:-mke2fs}

[ -f "$KERNEL" ] || { echo "mkrootfs: missing kernel: $KERNEL" >&2; exit 1; }
command -v "$MKE2FS" >/dev/null 2>&1 || {
    echo "mkrootfs: $MKE2FS not found (install e2fsprogs)" >&2
    exit 1
}

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/zenku-rootfs.XXXXXX")
cleanup() {
    rm -rf "$STAGE"
}
trap cleanup EXIT

cp "$KERNEL" "$STAGE/kernel"
cat > "$STAGE/README" <<'EOF'
Zenku root filesystem.

Read-only ext2 image built by scripts/mkrootfs.sh.
/kernel is the Raijin kernel ELF image.
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
truncate -s "${SIZE_MIB}M" "$OUT"

"$MKE2FS" -q -F -t ext2 -b 1024 -I 128 -L ZENKU \
    -O ^64bit,^metadata_csum,^dir_index \
    -d "$STAGE" "$OUT"

echo "mkrootfs: wrote $OUT ($(stat -c%s "$OUT") bytes)"
