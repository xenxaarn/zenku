#!/usr/bin/env bash
# Build a data disk image for the persistence and failure-path tests.
#
#   mkdataimg.sh <out.img> [size-mib] [label] [table]
#
# table is one of:
#   mbr       - one MBR partition (default)
#   gpt       - one GPT partition with a protective MBR
#   none      - a bare ext2 filesystem with no partition table
#   badmbr    - a disk with a boot signature but no usable partitions
#   badgpt    - a GPT image with a corrupted header checksum
#   recover   - a ZENKU_DATA volume with the needs-recovery flag set
#   full      - a ZENKU_DATA volume with every free inode exhausted
#
# An empty label produces an unlabeled ext2 volume, used to check that the
# kernel only mounts ZENKU_DATA read-write.
set -euo pipefail

OUT=${1:?usage: mkdataimg.sh <out.img> [size-mib] [label] [table]}
SIZE_MIB=${2:-16}
LABEL=${3-ZENKU_DATA}
TABLE=${4:-mbr}

MKE2FS=${MKE2FS:-mke2fs}
SFDISK=${SFDISK:-sfdisk}
DEBUGFS=${DEBUGFS:-debugfs}
START=2048
OFFSET=$((START * 512))

command -v "$MKE2FS" >/dev/null 2>&1 || {
    echo "mkdataimg: $MKE2FS not found (install e2fsprogs)" >&2
    exit 1
}
need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "mkdataimg: $1 not found (install e2fsprogs)" >&2
        exit 1
    }
}
need "$DEBUGFS"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MIB" status=none

label_args=()
if [ -n "$LABEL" ]; then
    label_args=(-L "$LABEL")
fi

# mke2fs with an explicit block count keeps the filesystem inside the
# partition and avoids the "offset without an explicit size" warning.
mkext2_partition() {
    local extra=("$@")

    need "$MKE2FS"
    "$MKE2FS" -q -t ext2 -b 1024 -F -E "offset=$OFFSET" \
        "${label_args[@]}" "${extra[@]}" "$OUT" "$(( (SIZE_MIB - 1) * 1024 ))"
}

make_mbr() {
    need "$SFDISK"
    printf 'label: dos\nstart=%s, type=83\n' "$START" | "$SFDISK" -q "$OUT"
    mkext2_partition "$@"
}

case "$TABLE" in
none)
    need "$MKE2FS"
    "$MKE2FS" -q -t ext2 -b 1024 -F "${label_args[@]}" "$OUT"
    ;;
mbr)
    make_mbr
    ;;
gpt)
    need "$SFDISK"
    printf 'label: gpt\nstart=%s, type=L\n' "$START" | "$SFDISK" -q "$OUT"
    mkext2_partition
    ;;
badmbr)
    # A valid boot signature with no partition entries: the scanner must fall
    # back to the whole disk, which has no ext2 superblock.
    printf '\x55\xaa' | dd of="$OUT" bs=1 seek=510 conv=notrunc status=none
    ;;
badgpt)
    need "$SFDISK"
    printf 'label: gpt\nstart=%s, type=L\n' "$START" | "$SFDISK" -q "$OUT"
    mkext2_partition
    # Flip a byte inside the GPT header CRC (offset 16 of LBA 1).
    printf '\xff' | dd of="$OUT" bs=1 seek=$((512 + 16)) conv=notrunc \
        status=none
    ;;
recover)
    make_mbr
    "$DEBUGFS" -w -R "feature +needs_recovery" \
        "$OUT?offset=$OFFSET" >/dev/null 2>&1
    ;;
full)
    make_mbr -N 32
    : > "$OUT.empty"
    for i in $(seq 1 64); do
        if ! "$DEBUGFS" -w -R "write $OUT.empty /filler$i" \
            "$OUT?offset=$OFFSET" >/dev/null 2>&1; then
            break
        fi
    done
    rm -f "$OUT.empty"
    ;;
*)
    echo "mkdataimg: unknown table: $TABLE" >&2
    exit 1
    ;;
esac

echo "mkdataimg: $OUT (${SIZE_MIB} MiB, label '${LABEL}', ${TABLE})"
