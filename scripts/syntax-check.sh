#!/usr/bin/env bash
# Fast syntax check for every source file in the tree.
#
#   syntax-check.sh [-v] [--link]
#
#   -v       print every command that succeeds (failures always print)
#   --link   also link stage3 and the kernel to catch undefined symbols and
#            broken linker scripts (still no ISO/ext2 build)
#
# C files are compiled with -fsyntax-only using the real build flags, taken
# from the Makefile so there is a single source of truth. Files shared by
# stage3 and the kernel (fs/, lib/) are checked with both flag sets.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

VERBOSE=0
LINK=0
for arg in "$@"; do
    case "$arg" in
    -v|--verbose) VERBOSE=1 ;;
    --link) LINK=1 ;;
    -h|--help)
        sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "syntax-check: unknown option: $arg" >&2
        exit 2
        ;;
    esac
done

have_make=0
if command -v make >/dev/null 2>&1; then
    have_make=1
fi

make_print() {
    if [ "$have_make" -eq 1 ]; then
        make -s --no-print-directory print-"$1" 2>/dev/null
    fi
}

CC=${CC:-$(make_print CC)}
CC=${CC:-gcc}
NASM=${NASM:-$(make_print NASM)}
NASM=${NASM:-nasm}
LD=${LD:-$(make_print LD)}
LD=${LD:-ld}

STAGE3_CFLAGS=${STAGE3_CFLAGS:-$(make_print STAGE3_CFLAGS)}
KERNEL_CFLAGS=${KERNEL_CFLAGS:-$(make_print KERNEL_CFLAGS)}
USER_CFLAGS=${USER_CFLAGS:-$(make_print USER_CFLAGS)}

# Source lists come from the Makefile so the check always covers exactly what
# the real build compiles.
STAGE3_CSRCS=${STAGE3_CSRCS:-$(make_print STAGE3_CSRCS)}
STAGE3_ASMS=${STAGE3_ASMS:-$(make_print STAGE3_ASMS)}
KERNEL_CSRCS=${KERNEL_CSRCS:-$(make_print KERNEL_CSRCS)}
KERNEL_ASMS=${KERNEL_ASMS:-$(make_print KERNEL_ASMS)}

if [ -z "$STAGE3_CFLAGS" ] || [ -z "$KERNEL_CFLAGS" ] ||
   [ -z "$STAGE3_CSRCS" ] || [ -z "$KERNEL_CSRCS" ]; then
    echo "syntax-check: could not read flags/sources from the Makefile" >&2
    exit 2
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/zenku-syntax.XXXXXX")
cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT

FAILURES=()
CHECKED=0

fail() {
    local what=$1 file=$2 output=$3
    printf 'FAIL  %-8s %s\n' "$what" "$file"
    if [ -n "$output" ]; then
        printf '%s\n' "$output" | sed 's/^/      /'
    fi
    FAILURES+=("$what $file")
}

check_c() {
    local label=$1 flags=$2 file=$3 output

    # shellcheck disable=SC2086
    if ! output=$("$CC" $flags -fsyntax-only "$file" 2>&1); then
        fail "C/$label" "$file" "$output"
        return 1
    fi
    CHECKED=$((CHECKED + 1))
    if [ "$VERBOSE" -eq 1 ]; then
        printf 'ok    %-8s %s\n' "C/$label" "$file"
    fi
    return 0
}

check_asm() {
    local file=$1 flags=$2 output
    local obj="$TMP/$(echo "$file" | tr '/' '_').o"

    # shellcheck disable=SC2086
    if ! output=$("$NASM" $flags -o "$obj" "$file" 2>&1); then
        fail "ASM" "$file" "$output"
        return 1
    fi
    if [ -n "$output" ] && [ "$VERBOSE" -eq 1 ]; then
        printf 'warn  ASM      %s\n%s\n' "$file" "$(printf '%s' "$output" | sed 's/^/      /')"
    fi
    CHECKED=$((CHECKED + 1))
    if [ "$VERBOSE" -eq 1 ]; then
        printf 'ok    %-8s %s\n' "ASM" "$file"
    fi
    return 0
}

check_shell() {
    local file=$1 output

    if ! output=$(bash -n "$file" 2>&1); then
        fail "SH" "$file" "$output"
        return 1
    fi
    CHECKED=$((CHECKED + 1))
    if [ "$VERBOSE" -eq 1 ]; then
        printf 'ok    %-8s %s\n' "SH" "$file"
    fi
    return 0
}

check_python() {
    local file=$1 output

    if ! output=$(python3 -c \
        'import sys; compile(open(sys.argv[1], "rb").read(), sys.argv[1], "exec")' \
        "$file" 2>&1); then
        fail "PY" "$file" "$output"
        return 1
    fi
    CHECKED=$((CHECKED + 1))
    if [ "$VERBOSE" -eq 1 ]; then
        printf 'ok    %-8s %s\n' "PY" "$file"
    fi
    return 0
}

# ---------------------------------------------------------------------------
# Discover sources
# ---------------------------------------------------------------------------
SOURCE_DIRS=()
for dir in boot kernel drivers fs lib user libc; do
    [ -d "$dir" ] && SOURCE_DIRS+=("$dir")
done

mapfile -t C_FILES < <(find "${SOURCE_DIRS[@]}" -name '*.c' -not -path '*/build/*' 2>/dev/null | sort)
# Only translation-unit assembly. The stage2 files are %include fragments and
# cannot be assembled on their own; stage1 and the kernel/stage3 entry files
# are complete units.
mapfile -t ASM_FILES < <(
    {
        find boot/stage1 -name '*.asm' 2>/dev/null
        find boot/stage3 kernel -name '*.asm' 2>/dev/null
    } | sort
)
mapfile -t SH_FILES < <(find scripts -maxdepth 1 -name '*.sh' 2>/dev/null | sort)
mapfile -t PY_FILES < <(find scripts tests -name '*.py' -not -path '*/__pycache__/*' 2>/dev/null | sort)

echo "syntax-check: cc=$CC nasm=$NASM ld=$LD check=$([ "$LINK" -eq 1 ] && echo with-link || echo syntax-only)"

# ---------------------------------------------------------------------------
# C
# ---------------------------------------------------------------------------
for file in "${C_FILES[@]}"; do
    case "$file" in
    boot/stage3/*|fs/*|lib/*) check_c stage3 "$STAGE3_CFLAGS" "$file" ;;
    esac
    case "$file" in
    kernel/*|fs/*|lib/*) check_c kernel "$KERNEL_CFLAGS" "$file" ;;
    esac
    if [ -n "$USER_CFLAGS" ]; then
        case "$file" in
        user/*|libc/*) check_c user "$USER_CFLAGS" "$file" ;;
        esac
    fi
done

# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------
STAGE2_BYTES=$(stat -c%s build/boot/stage2.bin 2>/dev/null || echo 8192)
for file in "${ASM_FILES[@]}"; do
    case "$file" in
    boot/stage1/*) check_asm "$file" "-f bin -Ox -I$ROOT -DSTAGE2_BYTES=$STAGE2_BYTES" ;;
    boot/stage2/*) check_asm "$file" "-f bin -Ox -I$ROOT" ;;
    *) check_asm "$file" "-f elf64" ;;
    esac
done

# ---------------------------------------------------------------------------
# Shell and Python helpers
# ---------------------------------------------------------------------------
for file in "${SH_FILES[@]}"; do
    check_shell "$file"
done
for file in "${PY_FILES[@]}"; do
    check_python "$file"
done

# ---------------------------------------------------------------------------
# Optional link check
# ---------------------------------------------------------------------------
if [ "$LINK" -eq 1 ]; then
    link_target() {
        local name=$1 flags=$2 ldflags=$3 script=$4
        shift 4
        local objs=()
        local file obj output

        for file in "$@"; do
            obj="$TMP/${name}_$(echo "$file" | tr '/' '_').o"
            case "$file" in
            *.asm)
                if ! output=$("$NASM" -f elf64 -o "$obj" "$file" 2>&1); then
                    fail "LD/$name" "$file" "$output"
                    return 1
                fi
                ;;
            *.c)
                # shellcheck disable=SC2086
                if ! output=$("$CC" $flags -c -o "$obj" "$file" 2>&1); then
                    fail "LD/$name" "$file (compile)" "$output"
                    return 1
                fi
                ;;
            esac
            objs+=("$obj")
        done

        # shellcheck disable=SC2086
        if ! output=$("$LD" $ldflags -T "$script" -o "$TMP/$name.elf" "${objs[@]}" 2>&1); then
            fail "LD/$name" "$script" "$output"
            return 1
        fi
        CHECKED=$((CHECKED + 1))
        if [ "$VERBOSE" -eq 1 ]; then
            printf 'ok    %-8s %s\n' "LD/$name" "$TMP/$name.elf"
        fi
        return 0
    }

    read -ra STAGE3_LINK_SRCS <<< "$STAGE3_ASMS $STAGE3_CSRCS"
    read -ra KERNEL_LINK_SRCS <<< "$KERNEL_ASMS $KERNEL_CSRCS"

    link_target stage3 "$STAGE3_CFLAGS" "-m elf_x86_64" linker/stage3.ld \
        "${STAGE3_LINK_SRCS[@]}"
    link_target kernel "$KERNEL_CFLAGS" "-m elf_x86_64 -nostdlib -z max-page-size=4096" \
        linker/kernel.ld "${KERNEL_LINK_SRCS[@]}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo
if [ "${#FAILURES[@]}" -ne 0 ]; then
    echo "syntax-check: ${#FAILURES[@]} failure(s)"
    for f in "${FAILURES[@]}"; do
        echo "  - $f"
    done
    exit 1
fi
echo "syntax-check: $CHECKED check(s) passed"
