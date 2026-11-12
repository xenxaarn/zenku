# Zenku

**Zenku** is a 64-bit Unix-like operating system and custom bootloader written
from scratch in C and x86_64 assembly. It targets BIOS first (SeaBIOS and real
hardware), adds a UEFI path later, and boots from a single `os.iso` that
contains the bootloader, the kernel, and a read-only ext2 root filesystem.

The kernel codename is **Raijin**.

## Status: Phase 1

Phase 1 completes the boot chain up to a running higher-half kernel:

- stage1 reads the stage2 payload from the El Torito extent
- stage2 collects E820, copies the BIOS 8x16 font, reads ISO9660, loads
  `STAGE3.BIN` and `ROOT.EXT2`, selects a 1024x768x32 VBE mode, enters long
  mode, and jumps to the 64-bit stage3 loader
- stage3 mounts the read-only ext2 image, loads `/kernel` (ELF64), builds the
  identity map, the 4 GiB direct map, and the 512 MiB kernel map, then hands
  off through a versioned `boot_info`
- the kernel runs at `0xFFFFFFFF80000000`, reports E820/framebuffer/initrd over
  COM1, and draws the Zenku/Raijin splash

Phase 2 adds GDT/TSS/IDT, exception handling, PIC/PIT, and physical/virtual
memory management.

The full 9-phase plan is in [`docs/PLAN.md`](docs/PLAN.md). The current boot
media contract is documented in [`docs/boot-protocol.md`](docs/boot-protocol.md).

## Progress

| Phase | Scope | Status |
|---|---|---|
| 0 | Build system, stage1, ISO, serial test harness | **Complete** |
| 1 | Stage1→stage2→stage3, E820/VBE/ISO9660, ext2+ELF64, long mode, higher-half kernel | **Complete** |
| 2 | GDT/TSS/IDT, exceptions, PIC/PIT, PMM/VMM/heap | Next |
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
make check           # fast syntax check of C/asm/shell/python sources
make check-link      # syntax check plus stage3 and kernel link checks
make layout          # validate the boot image layout
make doctor          # toolchain/tool discovery
```

`make run` uses a graphical QEMU window and mirrors all diagnostics to the
terminal over COM1. `make test` builds a variant that exits QEMU through
`isa-debug-exit` after the banner.

Expected serial output:

```
ZENKU STAGE1 OK
boot drive: 0xE0
ZENKU STAGE2 OK
stage2: e820 entries 0x00000007
stage2: font copied
stage2: ISO9660 mounted
stage2: STAGE3.BIN lba 0x00004026 size 0x00002581
stage2: stage3 loaded at 0x20000
stage2: ROOT.EXT2 lba 0x00000026 size 0x02000000
stage2: ROOT.EXT2 loaded at 0x04000000
stage2: VBE mode 0x00000400x00000300 bpp 0x00000020
ZENKU STAGE3 OK
stage3: ext2 1024-byte blocks, 4 groups, 8192 inodes
stage3: kernel inode 13, 57184 bytes
stage3: segment phys 0x200000..0x206250 (filesz 0x2211)
stage3: kernel phys 0x200000 size 0x6250 entry 0xffffffff80200000
stage3: paging ready (physmap 0xffff800000000000, kernel 0xffffffff80000000)
stage3: jumping to kernel
ZENKU KERNEL OK
kernel: Zenku 0.0.1 (Raijin), 64-bit higher half
kernel: e820 7 entries, usable 2047 MiB
kernel: framebuffer 1024x768 32bpp pitch 4096 at 0xfd000000
kernel: framebuffer banner drawn
kernel: entering idle loop
```

## Layout

```
boot/stage1/       Stage1 assembly (El Torito boot image)
boot/stage2/       BIOS/E820/VBE/ISO9660 stage
boot/stage3/       64-bit C ext2 + ELF64 loader (shared with the kernel tree)
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
