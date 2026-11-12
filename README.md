# Zenku

**Zenku** is a 64-bit Unix-like operating system and custom bootloader written
from scratch in C and x86_64 assembly. It targets BIOS first (SeaBIOS and real
hardware), adds a UEFI path later, and boots from a single `os.iso` that
contains the bootloader, the kernel, and a read-only ext2 root filesystem.

The kernel codename is **Raijin**.

## Status: Phase 0

Phase 0 establishes a reproducible build and proves the complete media path:

- 2 KiB El Torito stage1 in NASM
- serial (COM1) and BIOS console diagnostics
- `xorriso` ISO packaging
- headless QEMU test with a deterministic exit code
- host-toolchain fallback plus an optional real `x86_64-elf` cross compiler

Stage1 currently prints `ZENKU STAGE1 OK` and halts. Phase 1 adds stage2
(E820, VBE, ISO9660, ext2), the 32-bit loader, long mode, and the higher-half
kernel.

The full 9-phase plan is in [`docs/PLAN.md`](docs/PLAN.md). The current boot
media contract is documented in [`docs/boot-protocol.md`](docs/boot-protocol.md).

## Progress

| Phase | Scope | Status |
|---|---|---|
| 0 | Build system, stage1, ISO, serial test harness | **Complete** |
| 1 | Stage1→stage2→stage3, E820/VBE/ISO9660, ext2+ELF64, long mode, higher-half kernel | Next |
| 2 | GDT/TSS/IDT, exceptions, PIC/PIT, PMM/VMM/heap | Pending |
| 3 | Serial, PS/2 keyboard/mouse, framebuffer console, desktop skeleton | Pending |
| 4 | VFS, ext2 mount, ring 3, core syscalls | Pending |
| 5 | Scheduler, COW fork, exec/wait, libc, shell, coreutils | Pending |
| 6 | Graphical desktop, terminal window, mouse UI | Pending |
| 7 | UEFI loader and dual-boot ISO | Pending |
| 8 | SYSCALL fast path, SMEP/SMAP/NX, hardening, release | Pending |

## Requirements

Build essentials:

| Tool | Purpose | This machine |
|---|---|---|
| `nasm` | stage1/stage2 assembly | 3.02 |
| `xorriso` | ISO creation | 1.5.8 |
| `qemu-system-x86_64` | run/test | 11.1.3 |
| `mke2fs` (`e2fsprogs`) | ext2 root image (Phase 4) | 1.47.4 |
| host `gcc`/`clang`, `ld` | C fallback toolchain | gcc 16.2 |
| `make`, `bash`, `coreutils` | build | present |

Optional real cross toolchain:

```sh
make toolchain                 # installs into ~/.local/cross
source ~/.local/cross/env.sh   # puts x86_64-elf-* on PATH
```

Prerequisites for the cross build are listed by the script; on Fedora:

```sh
sudo dnf install gmp-devel mpfr-devel libmpc-devel bison flex texinfo
```

## Build and run

```sh
make                 # builds build/os.iso
make run             # qemu-system-x86_64 -cdrom build/os.iso -m 2G -serial stdio
make test            # headless boot; checks serial output and exit status
make doctor          # toolchain/tool discovery
```

`make run` uses a graphical QEMU window and mirrors all diagnostics to the
terminal over COM1. `make test` builds a variant that exits QEMU through
`isa-debug-exit` after the banner.

Expected serial output:

```
ZENKU STAGE1 OK
boot drive: 0xNN
```

## Layout

```
boot/stage1/       Stage1 assembly (El Torito boot image)
boot/stage2/       BIOS/E820/VBE/ISO9660 stage (Phase 1)
boot/stage3/       32-bit ext2 + ELF + long-mode loader (Phase 1)
kernel/            Higher-half kernel (Phase 1+)
drivers/           Serial, PS/2, framebuffer, tty, RTC
fs/                VFS, ext2, devfs, pipes
libc/              Freestanding user-space C library
user/              init, shell, coreutils
include/           Shared headers, boot ABI, syscall ABI
linker/            Kernel, stage3, and user linker scripts
scripts/           Toolchain, rootfs, ISO, tests, layout checks
tests/             Host and QEMU integration tests
docs/              Architecture, boot protocol, ABI, plan
```

## Design rules

1. Every stage validates its inputs and fails with a diagnostic, never a
   silent triple fault.
2. The bootloader and kernel share one documented `boot_info` ABI.
3. Kernel code is `-mcmodel=kernel`, `-mno-red-zone`, and built without libc
   or libgcc.
4. The build must work with the host compiler and with `x86_64-elf-gcc`.
5. Each phase ends with a bootable ISO and a passing test.

## License

MIT. See [`LICENSE`](LICENSE).
