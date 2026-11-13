#!/usr/bin/env bash
# Storage persistence test.
#
#   run-disk-test.sh <iso> [mode]
#
# mode:
#   ahci      - ZENKU_DATA ext2 on an MBR partition behind an AHCI controller
#   pata      - the same volume on a legacy IDE/PATA channel
#   gpt       - the same volume on a GPT partition
#   unlabeled - an ext2 volume without a label must mount read-only
#   nodisk    - no disk at all
#
# The ahci/pata/gpt modes boot twice: the first boot creates files under
# /data, the second proves they survived a power cycle.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
MODE=${2:-ahci}
[ -f "$ISO" ] || { echo "run-disk-test: missing ISO: $ISO" >&2; exit 1; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/zenku-disk.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

DISK="$WORK/data.img"
LOG="$WORK/serial.log"
CLEAN="$WORK/serial.txt"

case "$MODE" in
ahci)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA mbr >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
pata)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA mbr >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus pata)
    ;;
gpt)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA gpt >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
unlabeled)
    scripts/mkdataimg.sh "$DISK" 16 "" mbr >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
badmbr)
    scripts/mkdataimg.sh "$DISK" 16 "" badmbr >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
badgpt)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA badgpt >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
recover)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA recover >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
full)
    scripts/mkdataimg.sh "$DISK" 16 ZENKU_DATA full >/dev/null
    DRIVE_ARGS=(--drive "$DISK" --drive-bus ahci)
    ;;
nodisk)
    DRIVE_ARGS=()
    ;;
*)
    echo "run-disk-test: unknown mode: $MODE" >&2
    exit 1
    ;;
esac

boot() {
    local script=$1

    : > "$LOG"
    if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
        --wait 40 --wait-line "user: /init returned to the kernel" \
        --serial-send "$(printf '%b' "$script")" \
        "${DRIVE_ARGS[@]}" > "$LOG" 2>&1; then
        echo "run-disk-test: probe failed ($MODE)"
        cat "$LOG"
        exit 1
    fi
    tr -d '\r' < "$LOG" > "$CLEAN"
}

check() {
    if ! grep -q -- "$1" "$CLEAN"; then
        echo "FAIL ($MODE): missing: $1"
        tail -80 "$CLEAN"
        exit 1
    fi
}

# Prove the writer left a consistent filesystem behind. The data filesystem
# starts at LBA 2048 in every mode that uses a partition table.
fsck_ok() {
    command -v e2fsck >/dev/null 2>&1 || return 0
    if ! e2fsck -fn "$DISK?offset=1048576" >/dev/null 2>&1; then
        echo "FAIL ($MODE): e2fsck reported errors"
        e2fsck -fn "$DISK?offset=1048576"
        exit 1
    fi
}

case "$MODE" in
ahci|pata|gpt)
    boot "$(printf 'sh\recho zenku-persist > /data/marker\rmkdir /data/dir\recho nested > /data/dir/file\recho first > /data/append\recho second >> /data/append\rtouch /data/touched\rrm /data/touched\recho root-write > /root-write\rsync\rls /data\rls /data/dir\rcat /data/append\rcat /data/marker\rcat /data/touched\r')"
    check 'disk: /data mounted read-write'
    check 'marker'
    check 'dir/'
    check 'nested'
    check 'first'
    check 'second'
    check 'zenku-persist'
    check 'sh: cannot open /root-write'
    check 'cat: cannot open /data/touched'

    boot "$(printf 'sh\rcat /data/marker\rcat /data/dir/file\rcat /data/append\rcd /data\rmkdir rel\recho relative > rel/file\rcat rel/file\rrm rel/file\rrm rel\rcd /\rls /data\rsync\r')"
    check 'disk: /data mounted read-write'
    check 'zenku-persist'
    check 'nested'
    check 'first'
    check 'second'
    check 'relative'
    check 'marker'
    fsck_ok
    echo "PASS: $MODE volume survived a reboot"
    ;;
unlabeled)
    boot "$(printf 'sh\rcat /data/marker\recho x > /data/x\rsync\r')"
    check 'disk: /data mounted read-only'
    check 'disk: only a volume labelled ZENKU_DATA is mounted read-write'
    check 'sh: cannot open /data/x'
    echo "PASS: unlabeled ext2 mounted read-only"
    ;;
badmbr|badgpt)
    boot "$(printf 'sh\recho x > /data/x\r')"
    check 'disk: no ext2 data volume; /data is unavailable'
    check 'sh: cannot open /data/x'
    echo "PASS: $MODE rejected and /data stayed unavailable"
    ;;
recover)
    boot "$(printf 'sh\recho x > /data/recovered\rsync\r')"
    check 'disk: /data mounted read-only'
    check 'disk: ZENKU_DATA needs recovery or has unsupported features'
    check 'sh: cannot open /data/recovered'
    echo "PASS: recovery-flagged volume mounted read-only"
    ;;
full)
    boot "$(printf 'sh\rtouch /data/new\recho x > /data/new\rsync\r')"
    check 'disk: /data mounted read-write'
    check 'touch: cannot create /data/new'
    check 'sh: cannot open /data/new'
    fsck_ok
    echo "PASS: full filesystem reports ENOSPC without corruption"
    ;;
nodisk)
    boot "$(printf 'sh\recho x > /data/x\r')"
    check 'disk: no writable storage found; /data is unavailable'
    check 'sh: cannot open /data/x'
    echo "PASS: no disk leaves /data unavailable"
    ;;
esac
