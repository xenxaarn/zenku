#!/usr/bin/env bash
# Generate ext2 fixture images for the host unit tests.
#
#   mkimage.sh <output-dir>
#
# The images cover 1/2/4 KiB blocks, direct/single/double/triple indirect
# blocks, deep paths, and a large directory. Two deliberately damaged copies
# exercise the reject paths (bad superblock magic and a zeroed rec_len).
#
# Requirements: mke2fs, debugfs, dd, tr (e2fsprogs + coreutils).
set -euo pipefail

OUT=${1:?usage: mkimage.sh <output-dir>}
MKE2FS=${MKE2FS:-mke2fs}
DEBUGFS=${DEBUGFS:-debugfs}

command -v "$MKE2FS" >/dev/null 2>&1 || {
    echo "mkimage: $MKE2FS not found (install e2fsprogs)" >&2
    exit 1
}
command -v "$DEBUGFS" >/dev/null 2>&1 || {
    echo "mkimage: $DEBUGFS not found (install e2fsprogs)" >&2
    exit 1
}

mkdir -p "$OUT"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/zenku-hostimg.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

printf 'hello ext2\n' > "$WORK/hello.txt"
printf 'deep file\n' > "$WORK/deep.txt"
printf 'many\n' > "$WORK/many.txt"

# Constant byte 0x5A so the reader can verify arbitrary offsets without
# storing an expected copy on disk.
make_big() {
    local path=$1 size=$2

    dd if=/dev/zero bs=1M count="$size" status=none | tr '\0' 'Z' > "$path"
}

# build_image <blocksize> <size-mib> <big-mib> <large-dir-entries>
build_image() {
    local bs=$1
    local size_mib=$2
    local big_mib=$3
    local many=$4
    local name="ext2-${bs}"
    local img="$OUT/$name.img"
    local cmds="$WORK/$name.cmds"

    rm -f "$img"
    "$MKE2FS" -q -F -t ext2 -b "$bs" -I 128 \
        -O ^64bit,^metadata_csum,^extents,^inline_data \
        "$img" $((size_mib * 1024 * 1024 / bs))

    make_big "$WORK/big.bin" "$big_mib"

    {
        printf 'mkdir /deep\n'
        printf 'mkdir /deep/a\n'
        printf 'mkdir /deep/a/b\n'
        printf 'mkdir /deep/a/b/c\n'
        printf 'mkdir /deep/a/b/c/d\n'
        printf 'mkdir /deep/a/b/c/d/e\n'
        printf 'write %s /hello.txt\n' "$WORK/hello.txt"
        printf 'write %s /deep/a/b/c/d/e/f.txt\n' "$WORK/deep.txt"
        printf 'write %s /big.bin\n' "$WORK/big.bin"
        printf 'mkdir /many\n'
        for i in $(seq 0 $((many - 1))); do
            printf 'write %s /many/f%03d\n' "$WORK/many.txt" "$i"
        done
    } > "$cmds"

    "$DEBUGFS" -w -f "$cmds" "$img" > /dev/null 2>&1
    echo "mkimage: $name.img (${bs}-byte blocks, ${size_mib} MiB, big ${big_mib} MiB)"
}

build_image 1024 80 66 200   # 66 MiB reaches the triple-indirect zone
build_image 2048 24 4 200    # 4 MiB reaches the double-indirect zone
build_image 4096 24 6 200    # 6 MiB clears the 4 MiB single-indirect limit

# Unsupported features: extents must be rejected at mount time.
"$MKE2FS" -q -F -t ext4 -b 4096 -O ^64bit,^metadata_csum,extent \
    "$OUT/unsupported-extents.img" 4096

# Damaged superblock magic.
cp "$OUT/ext2-1024.img" "$OUT/bad-magic.img"
printf '\x00\x00' | dd of="$OUT/bad-magic.img" bs=1 seek=1080 conv=notrunc status=none

# Zeroed rec_len in the root directory's first entry (".").
cp "$OUT/ext2-1024.img" "$OUT/bad-rec-len.img"
python3 - "$OUT/bad-rec-len.img" <<'PY'
import struct
import sys

path = sys.argv[1]
with open(path, "r+b") as f:
    f.seek(1024)
    sb = f.read(1024)
    inode_size = struct.unpack_from("<H", sb, 88)[0] or 128
    first_data_block = struct.unpack_from("<I", sb, 20)[0]
    bs = 1024 << struct.unpack_from("<I", sb, 24)[0]

    f.seek((first_data_block + 1) * bs)
    gd = f.read(32)
    inode_table = struct.unpack_from("<I", gd, 8)[0]

    # Root inode is number 2: index 1 in group 0.
    inode_off = inode_table * bs + inode_size
    f.seek(inode_off)
    inode = f.read(inode_size)
    dir_block = struct.unpack_from("<I", inode, 40)[0]
    if dir_block == 0:
        raise SystemExit("bad-rec-len: root inode has no data block")

    # First directory entry rec_len lives at offset 4.
    f.seek(dir_block * bs + 4)
    f.write(b"\x00\x00")
PY

echo "mkimage: unsupported-extents.img, bad-magic.img, bad-rec-len.img"
