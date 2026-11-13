#!/usr/bin/env bash
# Build the read-only ext2 root filesystem embedded in the ISO.
#
#   mkrootfs.sh <kernel.elf> <out.ext2> [size-mib] [init.elf] [bin-dir]
#
# The image is populated from a staging directory, so it contains /kernel and
# a short README. Keep the feature set small: the Zenku ext2 reader supports
# 1/2/4 KiB blocks and rejects 64BIT, EXTENTS, INLINE_DATA and META_BG.
set -euo pipefail

KERNEL=${1:?usage: mkrootfs.sh <kernel.elf> <out.ext2> [size-mib]}
OUT=${2:?usage: mkrootfs.sh <kernel.elf> <out.ext2> [size-mib]}
SIZE_MIB=${3:-32}
INIT=${4:-}
BINDIR=${5:-}
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
if [ -n "$INIT" ]; then
    [ -f "$INIT" ] || { echo "mkrootfs: missing init: $INIT" >&2; exit 1; }
    cp "$INIT" "$STAGE/init"
    chmod 755 "$STAGE/init"
fi
mkdir -p "$STAGE/bin"
if [ -n "$BINDIR" ]; then
    [ -d "$BINDIR" ] || { echo "mkrootfs: missing bin dir: $BINDIR" >&2; exit 1; }
    for program in "$BINDIR"/*; do
        [ -f "$program" ] || continue
        cp "$program" "$STAGE/bin/$(basename "$program")"
        chmod 755 "$STAGE/bin/$(basename "$program")"
    done
fi
mkdir -p "$STAGE/etc"
mkdir -p "$STAGE/data"
cat > "$STAGE/README" <<'EOF'
Zenku root filesystem.

Read-only ext2 image built by scripts/mkrootfs.sh.
/kernel is the Thunder kernel ELF image.
EOF
cat > "$STAGE/etc/motd" <<'EOF'
Welcome to Zenku.

This root filesystem is a read-only ext2 image loaded by the bootloader.
Phase 5 runs /init, forks children, and executes programs from /bin.
EOF
cat > "$STAGE/etc/hostname" <<'EOF'
zenku
EOF
cat > "$STAGE/hello.txt" <<'EOF'
hello from the Zenku root filesystem
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
truncate -s "${SIZE_MIB}M" "$OUT"

"$MKE2FS" -q -F -t ext2 -b 1024 -I 128 -L ZENKU \
    -O ^64bit,^metadata_csum,^dir_index \
    -d "$STAGE" "$OUT"

echo "mkrootfs: wrote $OUT ($(stat -c%s "$OUT") bytes)"
