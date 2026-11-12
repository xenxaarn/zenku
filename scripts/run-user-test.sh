#!/usr/bin/env bash
# Ring-3 smoke test: boot, let /init run from ext2, verify its syscalls, then
# confirm the kernel and shell survive the process exit.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-user-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-user.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 25 --wait-line "user: /init returned to the kernel" \
    --serial-send "$(printf 'ls\rcat /hello.txt\r')" \
    > "$LOG" 2>&1; then
    echo "run-user-test: probe failed"
    cat "$LOG"
    exit 1
fi

check() {
    if ! grep -q "$1" "$LOG"; then
        echo "FAIL: missing: $1"
        tail -40 "$LOG"
        exit 1
    fi
}

check "user: entering ring 3"
check "init: hello from ring 3"
check "init: cs 0x1b"
check "init: pid 1 ppid 0"
check "init: bad user pointer rejected"
check "init: reading /etc/motd"
check "Welcome to Zenku."
check "user: exit status 0"
check "user: /init returned to the kernel"
check "hello from the Zenku root filesystem"

echo "PASS: /init ran in ring 3, used syscalls, and exited cleanly"
