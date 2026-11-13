# Zenku

**A 64-bit Unix-like operating system built from scratch in C and x86_64
assembly.**

Zenku boots on BIOS and UEFI systems from a single ISO, runs a higher-half
kernel with 4-level paging, and provides a small Unix environment: a read-only
ext2 root filesystem, a persistent writable ext2 volume mounted at `/data`,
processes, pipes, an interactive shell, core utilities, and a graphical
desktop. The kernel is codenamed **Thunder**.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Architecture](https://img.shields.io/badge/arch-x86__64-lightgrey.svg)
![Firmware](https://img.shields.io/badge/firmware-BIOS%20%7C%20UEFI-blue.svg)
![Release](https://img.shields.io/badge/release-v0.2.0-informational.svg)

![Zenku running fetch, the shell, a pipeline, and ps](docs/screenshots/hero.png)

## Overview

Zenku is a compact operating system for learning and experimentation, built
with production-style discipline: every boot stage validates its inputs,
kernel memory is isolated, user pointers are checked at the syscall boundary,
and the core subsystems are covered by automated tests.

There is no GRUB, Limine, Multiboot, or borrowed kernel. The boot chain,
kernel, VFS, scheduler, shell, and core utilities all live in this repository.
The result boots from one `build/os.iso` image on either firmware path.

## Highlights

| Area | What is implemented |
|---|---|
| **Boot** | Custom El Torito BIOS chain (stage1/2/3) and a native `BOOTX64.EFI`, sharing the `boot_info` ABI and one dual-entry ISO. |
| **Kernel** | Higher-half x86_64 at `0xFFFFFFFF80000000`, 4-level paging, 4 GiB direct map, bitmap frame allocator, 16 MiB heap. |
| **Hardening** | CPUID-gated NX, SMEP, and SMAP; W^X user mappings; validated usercopy with `stac`/`clac`; heap redzones; `SYSCALL`/`SYSRET` fast path with `int 0x80` retained. |
| **Processes** | Per-process address spaces, COW `fork`, `execve`, blocking `wait4`, `brk`, `mmap`/`munmap`, guarded stacks, 100 Hz round-robin. |
| **Storage** | Read-only ext2 root plus a writable ext2 volume at `/data`, discovered through PCI/AHCI or legacy PATA, with MBR/GPT parsing, a POSIX-style VFS, devfs, and 4 KiB pipes with `EAGAIN`, `EPIPE`, and EOF. |
| **Userland** | Freestanding libc, shared `crt0`, `/init`, `/bin/sh` with pipelines and `>`/`>>` redirection, plus coreutils including `mkdir`, `rm`, `touch`, `sync`, and `poweroff`. |
| **Desktop** | Framebuffer terminal with ANSI color, draggable and resizable window, taskbar, RTC top bar, and a 60 Hz dirty-rect compositor. |
| **Firmware** | ACPI RSDP/FADT discovery on both paths; S5 poweroff with FADT, 8042, and PCI reset fallbacks. |
| **Verification** | Native string/ext2/writer tests plus BIOS, UEFI, fault, input, serial, filesystem, storage-persistence, syscall, process, and shell QEMU suites. |

## Screenshots

| Interactive shell | Scheduler and processes | ext2 and devfs |
|:---:|:---:|:---:|
| ![Zenku shell](docs/screenshots/shell.png) | ![Process scheduling](docs/screenshots/scheduler.png) | ![Filesystem and devices](docs/screenshots/filesystem.png) |

## Architecture

```
BIOS / SeaBIOS                            UEFI / OVMF
  └─ stage1  El Torito                      └─ BOOTX64.EFI  El Torito FAT
      └─ stage2  E820, VBE, ISO9660             └─ GOP, memory map, SimpleFileSystem
          └─ stage3  ext2, ELF64                    └─ ext2, ELF64, page tables
              └──────────────────┬───────────────────────┘
                           Thunder kernel
                             ├─ memory: PMM, VMM, heap
                             ├─ storage: ext2, writable /data, VFS, devfs, pipes
                             ├─ execution: ELF loader, syscalls, user copies
                             ├─ processes: scheduler, COW fork, exec, wait
                             ├─ hardening: NX, SMEP/SMAP, W^X, heap redzones
                             └─ desktop: framebuffer, terminal, RTC, compositor
```

| Region | Virtual address | Purpose |
|---|---|---|
| User text | `0x0000000000400000` | Static ELF images |
| User heap | `0x0000000010000000` | Demand-zero `brk` heap |
| User mmap | `0x0000600000000000` | Anonymous mappings |
| User stack | `0x00007FFFFFF00000` | Guarded, downward-growing stack |
| Direct map | `0xFFFF800000000000` | 4 GiB physical memory window |
| Kernel image | `0xFFFFFFFF80000000` | Kernel text, data, and BSS |
| Kernel heap | `0xFFFFFFFFA0000000` | 16 MiB general-purpose heap |

## Getting started

### Requirements

A Linux host with a C compiler and binutils, plus:

| Tool | Purpose |
|---|---|
| `nasm` | Bootloader and kernel assembly |
| `clang` / `lld-link` | UEFI loader (`BOOTX64.EFI`) |
| `xorriso` | ISO image creation |
| `mke2fs` / `e2fsprogs` | Root filesystem and data-disk images |
| `mtools` / `dosfstools` | EFI FAT boot image |
| `sfdisk` / `debugfs` | Storage test disks (`util-linux`, `e2fsprogs`) |
| `qemu-system-x86_64` | Emulation and integration tests |
| `OVMF` | UEFI firmware for `make test-uefi` |
| `python3` | QEMU probe and image inspection scripts |
| `make`, `bash`, coreutils | Build and test scripts |

A real `x86_64-elf` cross toolchain is optional. The build falls back to the
host compiler with freestanding flags.

```sh
make toolchain                 # optional: install to ~/.local/cross
source ~/.local/cross/env.sh   # put x86_64-elf-* on PATH
```

### Build and run

```sh
make          # build build/os.iso
make run      # boot in a graphical QEMU window, serial on stdio
make debug    # boot with a QEMU GDB stub (-s -S)
```

`make run` uses 2 GiB of RAM and mirrors kernel diagnostics to the terminal
over COM1.

### Tests

```sh
make test       # headless BIOS boot and serial assertions
make test-uefi  # headless UEFI boot under OVMF
make test-host  # native string, ext2 reader, and ext2 writer tests
make test-disk  # storage persistence and failure paths
make check      # syntax checks; make check-link adds link checks
```

<details>
<summary>Full test matrix</summary>

| Target | Coverage |
|---|---|
| `make test` | Boot chain, kernel self-tests, and serial assertions |
| `make test-faults` | `#DE`, `#UD`, `#GP`, and `#PF` panic handling |
| `make test-input` | PS/2 keyboard and mouse path |
| `make test-serial` | COM1 receive over IRQ4 |
| `make test-fs` | ext2, VFS, and devfs |
| `make test-user` | Ring-3 entry, syscalls, `brk`/`mmap`, usercopy, and W^X |
| `make test-proc` | `fork`, `exec`, `wait`, pipes, preemption, and COW stress |
| `make test-sh` | Shell builtins, coreutils, and pipelines |
| `make test-uefi` | OVMF boots `BOOTX64.EFI` into the same kernel and userland |
| `make test-disk-ahci` | Writes to `/data` over AHCI, reboots, and verifies persistence |
| `make test-disk-pata` | The same persistence check over legacy PATA |
| `make test-disk-gpt` | Persistence with a GPT data disk |
| `make test-disk-faults` | Unlabeled, corrupt MBR/GPT, recovery-pending, full, and missing disks |
| `make test-host` | 496 native checks over string/mem, ext2 reading, and ext2 writing |
| `make check` | C, assembly, shell, and Python syntax checks |
| `make check-link` | Syntax checks plus stage3 and kernel link checks |
| `make layout` | Boot image layout and linker-symbol validation |

</details>

`make test-host` builds the shared `lib/string.c`, `fs/ext2.c`, and
`fs/ext2_write.c` sources for the host with `-fno-builtin`, generates
1/2/4 KiB ext2 fixture images, and checks deep paths, large directories,
indirect blocks, corrupt-directory rejection, and the full writer path. The
writer suite creates, appends, truncates, unlinks, makes and removes
directories, crosses the single- and double-indirect boundaries, then runs
`e2fsck -fn` over the result. It needs no QEMU or root privileges.

## Using Zenku

The kernel console accepts `help`, `fetch`, `mem`, `ticks`, `uptime`,
`version`, `clear`, `colors`, `ls`, `cat`, `stat`, `devtest`, `init`, `sh`,
and `ps`.

`sh` starts the userland shell. It supports `cd`, `clear`, `exit`, `help`,
and `pwd` builtins, external programs in `/bin`, and pipelines of up to four
stages:

```text
sh> cd /bin
sh> pwd
sh> ls
sh> cd ..
sh> cat /hello.txt
sh> wc < /hello.txt
sh> cat /etc/motd | head -n 1
sh> echo hello from user space | wc
sh> mkdir /data/notes
sh> echo first entry > /data/notes/log.txt
sh> echo second entry >> /data/notes/log.txt
sh> touch /data/notes/marker
sh> rm /data/notes/marker
sh> sync
sh> sleep 1
sh> ps
sh> poweroff
```

`poweroff` enters ACPI S5 through the FADT PM1 control registers. If the
firmware does not cut power, the kernel falls back to the FADT reset register,
the 8042 controller, the PCI reset control register, and finally a halt.

### Persistent data volume

The root filesystem stays read-only, so the live ISO can be booted from
read-only media without modifying it. A second disk provides persistent
storage:

- The kernel probes PCI for an AHCI controller, then falls back to the legacy
  PATA channels, and parses MBR or GPT partition tables.
- An ext2 volume whose label is exactly `ZENKU_DATA` is mounted at `/data`
  read-write, provided it needs no journal recovery and has no unsupported
  read-only feature.
- An unlabeled ext2 volume is still mounted at `/data`, but read-only. Zenku
  never formats a disk automatically.
- Writes are write-through: data blocks, inode and bitmap metadata, and the
  superblock are updated in order and followed by a device flush. There is no
  journal, so a power loss can leave a partially updated filesystem.

The shell supports `>`, `>>`, and `<` redirection; the write forms require a
writable mount. The coreutils include `mkdir`, `rm`, `touch`, and `sync`.

The desktop is mouse-driven. Drag the title bar to move the window, drag the
bottom-right grip to resize (the scrollback reflows), click the red button to
close, and use the taskbar `console` button to reopen it. The top bar shows
live uptime, CMOS clock, task count, and memory use.

## Repository layout

```
boot/        BIOS stages and the UEFI application
kernel/      higher-half kernel: arch, memory, exec, filesystem, scheduler, syscalls
drivers/     PIC, PIT, PS/2, mouse, serial, RTC, ACPI, PCI, AHCI, and PATA
fs/          shared ext2 reader, kernel-only ext2 writer, and partition parser
lib/         freestanding string and kernel I/O helpers
libc/        userland C library and syscall ABI
user/        crt0, init, shell, and coreutils
include/     kernel and boot ABI headers
linker/      kernel, stage3, and user linker scripts
scripts/     build, image, rootfs, data-disk, QEMU, and test tooling
tests/       host unit tests
docs/        plan, boot protocol, and screenshots
third_party/ vendored licenses and provenance notes
```

## Documentation

- [`docs/PLAN.md`](docs/PLAN.md) — architecture, phase plan, and design notes
- [`docs/boot-protocol.md`](docs/boot-protocol.md) — boot media and `boot_info` ABI

## Contributing

Issues and pull requests are welcome. Before submitting a change:

```sh
make check
make check-link
make test-host
make test-disk
make test
```

Keep patches focused, match the surrounding C style, and update the relevant
documentation when behavior changes.

## License

MIT. See [`LICENSE`](LICENSE).
