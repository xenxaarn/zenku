#!/usr/bin/env bash
# Shell smoke test: start /bin/sh from the kernel prompt, run a builtin and
# several external commands, then exit back to the kernel.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/os.iso}
[ -f "$ISO" ] || { echo "run-sh-test: missing ISO: $ISO" >&2; exit 1; }

LOG=$(mktemp "${TMPDIR:-/tmp}/zenku-sh.XXXXXX")
trap 'rm -f "$LOG"' EXIT

if ! python3 "$ROOT/scripts/qemu-probe.py" "$ISO" \
    --wait 40 --wait-line "user: /init returned to the kernel" \
    --serial-send "$(printf 'sh\rhelp\rpwd\rcd /bin\rpwd\rls\rcd ..\rpwd\r/bin/pwd\rls /\recho shell works\runame\rcat /hello.txt\recho hi | wc\rcat /etc/motd | head -n 1\rwc < /hello.txt\rcat < /hello.txt | head -n 1\recho hidden > /dev/null\recho visible\recho hi > /hello.txt\rsleep 0\recho after sleep\rps\rexit\r')" \
    > "$LOG" 2>&1; then
    echo "run-sh-test: probe failed"
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

check "sh> "
check "shell: builtins: cd, clear, exit, help, pwd"
check "sh> /bin"
check "sh> /"
check "pwd"
check "sh> /"
check "hello.txt"
check "shell works"
check "Zenku Thunder x86_64"
check "hello from the Zenku root filesystem"
check "1 1 3"
check "Welcome to Zenku."
check "1 6 37"
check "visible"
check "sh: cannot open /hello.txt"
check "after sleep"
check "PID PPID STATE NAME"
check "run   sh"
check "user: /bin/sh returned to the kernel"

echo "PASS: /bin/sh ran builtins, external commands, pipelines, and exited"
