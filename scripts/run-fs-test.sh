#!/usr/bin/env bash
# Filesystem smoke test: list the ext2 root, cat /etc/motd, and stat a file
# over COM1. Verifies ramdisk -> ext2 -> VFS -> console end to end.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-fs-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-fs.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 20 --wait-line "desktop: terminal ready" \
    --serial-send "$(printf 'ls\rls /dev\rstat /hello.txt\rstat /dev/null\rdevtest\rcat /etc/motd\r')" \
    > "$LOG" 2>&1; then
    echo "run-fs-test: probe failed"
    cat "$LOG"
    exit 1
fi

check() {
    if ! grep -q "$1" "$LOG"; then
        echo "FAIL: missing: $1"
        tail -30 "$LOG"
        exit 1
    fi
}

check "vfs: ext2 mounted"
check "README"
check "etc/"
check "Welcome to Zenku."
check "regular file"
check "size   37 bytes"
check "console"
check "character device"
check "devtest: write /dev/null: ok"
check "devtest: read /dev/zero: ok"
check "devtest: ext2 write rejected: ok"

echo "PASS: ext2 rootfs mounted; ls, cat, and stat work"
