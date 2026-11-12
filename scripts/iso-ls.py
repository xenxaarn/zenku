#!/usr/bin/env python3
"""List ISO9660 root directory entries for boot debugging.

Usage: iso-ls.py [build/os.iso]
"""
import struct
import sys

SECTOR = 2048


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "build/os.iso"
    with open(path, "rb") as f:
        f.seek(16 * SECTOR)
        pvd = f.read(SECTOR)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            print(f"{path}: not an ISO9660 primary volume", file=sys.stderr)
            return 1

        rec = pvd[156:190]
        extent = struct.unpack("<I", rec[2:6])[0]
        size = struct.unpack("<I", rec[10:14])[0]
        print(f"root extent={extent} size={size}")

        f.seek(extent * SECTOR)
        data = f.read(size)

    pos = 0
    while pos < len(data):
        length = data[pos]
        if length == 0:
            pos = (pos | (SECTOR - 1)) + 1
            continue
        rec = data[pos:pos + length]
        name_len = rec[32]
        name = rec[33:33 + name_len].decode("latin-1")
        lba = struct.unpack("<I", rec[2:6])[0]
        fsize = struct.unpack("<I", rec[10:14])[0]
        flags = rec[25]
        kind = "dir " if flags & 2 else "file"
        print(f"{pos:6d}  {kind}  lba={lba:8d}  size={fsize:10d}  {name}")
        pos += length
    return 0


if __name__ == "__main__":
    sys.exit(main())
