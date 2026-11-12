#!/usr/bin/env bash
# Process scheduler smoke test: fork, exec, wait, pipe inheritance, and timer
# preemption, then return to the kernel shell.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-proc-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-proc.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 60 --wait-line "user: /init returned to the kernel" \
    --serial-send "$(printf 'ps\rls /bin\rcat /hello.txt\r')" \
    > "$LOG" 2>&1; then
    echo "run-proc-test: probe failed"
    cat "$LOG"
    exit 1
fi

check() {
    if ! grep -q "$1" "$LOG"; then
        echo "FAIL: missing: $1"
        tail -60 "$LOG"
        exit 1
    fi
}

check "user: entering ring 3"
check "init: pid 1 ppid 0"
check "init: forked child"
check "hello from exec"
check "init: child exec and exit ok"
check "init: pipe across fork ok"
check "init: child exit status 7 ok"
check "cow: child inherited value"
check "cow: child private write ok"
check "cow: parent value preserved"
check "cow: child pipe fds ok"
check "cow: parent fd array preserved"
check "cow: 20 fork stress ok"
check "preempt: child B ran"
check "preempt: child A finished"
check "preempt: child A reaped"
check "preempt: child B reaped"
check "user: /init returned to the kernel"
check "hello from the Zenku root filesystem"
check "echo"

# Timer preemption must let child B run while child A is still spinning.
b_line=$(grep -n "preempt: child B ran" "$LOG" | head -1 | cut -d: -f1)
a_line=$(grep -n "preempt: child A finished" "$LOG" | head -1 | cut -d: -f1)
if [ -z "$b_line" ] || [ -z "$a_line" ] || [ "$b_line" -ge "$a_line" ]; then
    echo "FAIL: child B did not run before child A finished (no preemption?)"
    tail -60 "$LOG"
    exit 1
fi

echo "PASS: fork/exec/wait, fd inheritance, and timer preemption work"
