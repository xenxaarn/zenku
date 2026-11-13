# Zenku — 64-bit Unix-like Kernel & Custom Bootloader

**Status:** Phase 9 complete — `v0.2.0` (persistent storage: AHCI/PATA, writable ext2 `/data`)
**Repository:** self-contained; all sources, build scripts, and documentation live here.
**Plan file:** this document is the canonical project plan.
**Target:** bootable `os.iso` for BIOS (SeaBIOS/real hardware) and UEFI (OVMF)
**Default name:** `Zenku`, kernel codename `Thunder` (branding centralized in `include/version.h`)

---

## 1. Decisions locked from review

| Decision | Choice |
|---|---|
| Firmware | BIOS chain plus a native UEFI application; both boot the same ISO |
| Toolchain | Bootstrap real `x86_64-elf` via script; host `gcc`/`clang` freestanding fallback |
| Userland | Tiny libc + `sh` + coreutils + graphical desktop |
| CPU model | Uniprocessor x86_64, 4-level paging, preemptive round-robin |
| Root filesystem | ext2 read-only image embedded in the ISO, loaded into RAM by the bootloader |
| Boot protocol | Custom, documented `boot_info` struct; no GRUB/Limine/Multiboot |
| Syscalls | `int 0x80` compatibility gate plus `SYSCALL`/`SYSRET` fast path with a per-CPU GS block |
| ISO | `xorriso` El Torito no-emulation, boot-info-table patched |
| Tooling verified present | `gcc 16`, `clang 23`, `nasm 3.02`, `ld 2.47`, `xorriso 1.5.8`, `qemu 11.1`, `mke2fs 1.47`, `OVMF`, `ld.lld`/`lld-link` |

### Reality check

"Production-grade" here means rigorous engineering: transactional boot paths, bounds
checks, explicit error handling, page-level isolation, user-pointer validation,
self-tests, and a documented ABI. It does **not** mean Linux parity. This plan
deliberately excludes SMP, networking, disk write support, signals beyond
`wait`/exit semantics, and POSIX certification. The first release is a
~20–25k LOC uniprocessor OS that is genuinely usable, testable, and extensible.

Honest effort estimate: **9 phases, roughly 20–25 focused work sessions**.

---

## 2. Name

The project is **Zenku**, kernel codename
**Thunder**. The alternatives below remain on record in case of a future rename;
branding is centralized in `include/version.h`.

| Name | Meaning / rationale |
|---|---|
| **Thunder** | English name for the thunder-god motif; matches Zenitsu's Thunder Breathing |
| **Zenku** | Zenitsu + kernel; short, unique binary/tag name |
| **Zenith** | "highest point"; fits a kernel linked at the top of the address space |
| **Denkō** | "lightning" |
| **Kaminari** | "thunder" |
| **Hikari** | "light" |

Changing the name later is one edit: `OS_NAME`, `OS_CODENAME`, `OS_VERSION` live
only in `include/version.h`; the ISO volume label is a Makefile variable.

---

## 3. System architecture

### 3.1 Boot chain

```
BIOS/SeaBIOS
  │  El Torito no-emulation, 2048-byte virtual sectors
  ▼
stage1  (16-bit real mode, 2 KiB, phys 0x7C00)
  │  · validates DL / boot drive and int13 extensions
  │  · reads its own El Torito Boot Info Table (bi_file LBA, patched by xorriso)
  │  · loads stage2 from the boot-image extent, with DAP retries
  │  · verifies stage2 magic/checksum, enabling A20 first
  ▼
stage2  (16-bit + unreal mode, ≤64 KiB, phys 0x9000)
  │  · inits COM1 and prints progress/errors
  │  · INT 15h E820 memory map  → boot_params @ 0x6000
  │  · INT 10h VBE mode selection + LFB info (preference list, fallback text)
  │  · minimal ISO9660 reader (PVD → root dir → file extents)
  │  · loads /ROOT.EXT2 to phys 0x04000000 (16 MiB)
  │  · loads /STAGE3.BIN to phys 0x20000
  │  · enters 32-bit protected mode, flat segments, jumps to stage3
  ▼
stage3  (32-bit C + asm, ≤512 KiB, phys 0x20000)
  │  · validates boot_params, initrd magic and ELF headers
  │  · reuses fs/ext2 core over a memory block device
  │  · opens /kernel, parses ELF64, loads PT_LOAD to p_paddr
  │  · zeroes .bss, builds PML4/PDPT/PD: identity 0–1 GiB, kernel high half,
  │    direct physical map (4 GiB with 2 MiB pages)
  │  · enables PAE + long mode, far-jumps to 64-bit trampoline
  ▼
kernel  (64-bit, linked at 0xFFFFFFFF80000000, LMA 0x00200000)
     · _start: stack, clear BSS, GDT/TSS, IDT, CPU features
     · copies boot_info, initializes PMM/VMM/heap, drivers, VFS
     · mounts ext2 ramdisk, execs /init, starts scheduler
```

The UEFI path reaches the same kernel entry with a shorter chain:

```
UEFI/OVMF
  │  El Torito no-emulation FAT image
  ▼
BOOTX64.EFI  (PE32+ application, boot/uefi)
  │  · SimpleFileSystem → ROOT.EXT2 → shared fs/ext2 core → /kernel
  │  · GOP mode selection (1024×768, 8:8:8 channels)
  │  · EFI memory map → merged E820 entries clamped to 4 GiB
  │  · builds the same page tables and boot_info with BOOT_FLAG_UEFI
  │  · ExitBootServices, loads CR3, jumps with boot_info in RDI
  ▼
kernel  (same entry, ABI, and userland as the BIOS path)
```

### 3.2 ISO layout and why it is robust

```
build/os.iso
├── boot/stage1.bin     ← El Torito boot image:
│                           2048 B stage1 + stage2 (max 64 KiB)
│                           Boot Info Table at file offset 8 (patched by xorriso)
├── STAGE3.BIN          ← 32-bit loader (flat binary)
├── ROOT.EXT2           ← 16 MiB ext2 root filesystem
├── efi.img             ← FAT16 El Torito image: EFI/BOOT/BOOTX64.EFI + ROOT.EXT2
├── /kernel             ← (inside ROOT.EXT2, loaded by either boot path)
└── README.TXT
```

* Stage1 never assumes its ISO LBA. xorriso patches `bi_file` (offset 12) and
  `bi_length` (offset 16) into the boot image; stage1 reads its own stage2
  sectors from `bi_file + 1`.
* Sector geometry is discovered with `INT 13h AH=48h`; 2048-byte CD sectors are
  used when reported, with a 512-byte translation retry and a hard-disk-emulation
  fallback build target (`make iso ISO_EMUL=hd`) for picky firmware.
* Stage2 only needs one ISO9660 directory level. File names are matched
  case-insensitively against `ROOT.EXT2;1` and `STAGE3.BIN;1`.
* All stages print a distinct error code over serial and BIOS/VBE console.
* Kernel and ext2 image live as real ISO files, so `os.iso` stays small and
  inspectable.

### 3.3 Physical memory map (BIOS boot-time contract)

| Range | Use |
|---|---|
| `0x00000000–0x000004FF` | IVT / BIOS data area (reserved) |
| `0x00001000–0x00001FFF` | stage3 PML4; UEFI places its PML4 copy here |
| `0x00002000–0x00002FFF` | stage3 PDPTs |
| `0x00003000–0x00004FFF` | stage3 PDs (identity, kernel, physmap) |
| `0x00005000–0x00005FFF` | E820 map (max 128 × 24 B) |
| `0x00006000–0x00006FFF` | `boot_params` shared by stage2/stage3 |
| `0x00007C00–0x000083FF` | stage1 (2 KiB) |
| `0x00009000–0x00018FFF` | stage2 (≤64 KiB) |
| `0x00020000–0x0009FFFF` | stage3 (≤512 KiB, linker assert) |
| `0x000A0000–0x000FFFFF` | VGA/ROM hole (reserved) |
| `0x00100000–…` | kernel ELF physical image (LMA) |
| `0x04000000–0x04FFFFFF` | ext2 root image (16 MiB) |
| LFB physical base | VBE linear framebuffer (reserved from PMM) |
| remaining usable RAM | PMM frames ≥ 4 KiB, excluding all of the above |

On UEFI the loader allocates ROOT.EXT2, the kernel segments, `boot_info`, the
E820 buffer, the font, and the page tables with `AllocatePages`, then marks
each range reserved in the generated E820 map. The PML4 template is copied to
`0x00001000` after `ExitBootServices` because the kernel ABI fixes
`VMM_PML4_PHYS`; its lower page-table levels remain in loader-allocated
memory.

Minimum RAM: 256 MiB. Recommended/tested: 2 GiB.

### 3.4 Virtual address space

| Range | Mapping |
|---|---|
| `0x0000000000000000–0x00007FFFFFFFFFFF` | user lower half, per-process |
| `0x0000000000400000` | user ELF text base |
| `0x0000000010000000–0x0000000013FFFFFF` | demand-zero `brk` heap (64 MiB) |
| `0x0000600000000000–0x000060003FFFFFFF` | anonymous mmap region (1 GiB) |
| `0x00007FFFFFF00000` | user stack top (1 MiB growth, guard below) |
| `0xFFFF800000000000–0xFFFFBFFFFFFFFFFF` | direct physical map (`phys_to_virt`) |
| `0xFFFFFFFF80000000–0xFFFFFFFF9FFFFFFF` | kernel image (linked here, 512 MiB) |
| `0xFFFFFFFFA0000000–0xFFFFFFFFBFFFFFFF` | kernel heap / slab |
| `0xFFFFFFFFC0000000–0xFFFFFFFFDFFFFFFF` | vmalloc / MMIO mappings |
| `0xFFFFFFFFE0000000–0xFFFFFFFFEFFFFFFF` | per-CPU data, boot info |
| `0xFFFFFFFFF0000000–0xFFFFFFFFFFFFFFFF` | fixmap (trampoline, LAPIC, debug) |

* Kernel is compiled `-mcmodel=kernel`; its PML4 entry 511 is shared by every
  process address space, so a syscall/interrupt never changes CR3.
* User PML4 entries 0–255 are per-process. `fork` clones them copy-on-write.
* Identity map of the first 1 GiB exists only inside the bootloader; the kernel
  drops it once the direct map is active.

### 3.5 Boot ABI (`include/boot_info.h`)

```c
#define BOOT_MAGIC 0x5A454E4B55303031ULL   /* "ZENKU001" */
#define BOOT_ABI_VERSION 1

struct boot_info {
    uint64_t magic;
    uint32_t version;
    uint32_t flags;              /* FB | INITRD | UEFI | TEXT_FALLBACK */
    uint64_t e820_addr;          /* physical */
    uint32_t e820_count;
    uint32_t e820_entry_size;    /* 24 */

    uint64_t fb_addr;            /* physical LFB, 0 if none */
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    uint8_t  fb_red_pos,  fb_red_size;
    uint8_t  fb_green_pos,fb_green_size;
    uint8_t  fb_blue_pos, fb_blue_size;
    uint8_t  fb_rsvd_pos, fb_rsvd_size;

    uint64_t initrd_addr;        /* physical ext2 image */
    uint64_t initrd_size;
    uint64_t kernel_phys_base;
    uint64_t kernel_size;
    uint64_t kernel_entry;       /* virtual */
    uint64_t rsdp_addr;          /* 0 if not found */
    uint64_t boot_drive;         /* BIOS DL */
    char     cmdline[256];
} __attribute__((packed));
```

Passing convention: stage3 places this struct in the low physical page it
reserves and passes its **physical** address in `RDI`. The kernel, after
building the direct map, copies it to the per-CPU/boot area and validates the
magic/version before touching any field.

### 3.6 Kernel subsystems

**CPU / arch (`kernel/arch/x86_64`, `kernel/core`)**
- GDT: null, kernel code/data, user code/data, TSS; TSS.RSP0 updated on switch.
- IDT: 256 vectors; common asm stubs save a full trap frame, dispatch to C,
  `iretq`. Dedicated IST stacks for #DF and NMI.
- Exceptions: decode all 32, including page-fault CR2/error bits. Diagnostics
  print vector, error, RIP/CS/RFLAGS/RSP/CR2 and a frame-pointer backtrace.
- CPUID feature detection; enable NX (EFER.NXE), WP, SSE (CR0.MP/CR4.OSFXSR/
  OSXMMEXCPT), and SMEP/SMAP when the CPU supports them. `fxsave`/`fxrstor`
  per task.
- PIC remap (0x20–0x2F), mask spurious IRQs, PIT channel 0 at 100 Hz, RTC
  wall clock, interrupt-safe spinlocks (`irq_save`/`irq_restore`).

**Memory (`kernel/mm`)**
- PMM: E820-driven bitmap, 4 KiB frames, reserve low 1 MiB, kernel image,
  boot data, initrd, LFB. Contiguous allocation for page tables.
- `struct page`: refcount + flags for COW and shared pages.
- VMM: 4-level walker, map/unmap/protect/translate, per-space PML4, kernel-half
  sharing, TLB shootdown not needed (UP), guard pages.
- Heap: slab classes 16–4096 B plus large page-backed allocations; optional
  redzones under `CONFIG_DEBUG_HEAP`.
- VMA list: code/data/heap/stack/mmap ranges used by page faults, `brk`, `mmap`,
  and user-pointer validation.
- Page fault policy: COW writable copy, demand-zero anonymous pages, stack
  growth up to the guard, otherwise SIGSEGV-equivalent task kill.
- `copy_from_user`/`copy_to_user` with VMA range checks plus an exception-table
  fixup so a bad pointer returns `-EFAULT` instead of panicking.

**Filesystem (`fs/`)**
- VFS: inode/file/superblock ops, refcounted open files, fd table in `struct
  task`, path walk with `.`/`..`, root + cwd, no mount namespace in v1.
- ext2 read-only core shared by stage3 and kernel:
  - 1/2/4 KiB blocks, group descriptors, inode table, direct/single/double/
    triple indirect blocks;
  - directory iteration with rec_len validation;
  - rejects unknown incompatible feature bits, supports 64-bit size fields
    read-only;
  - stage3 uses a memory `blockdev`, kernel uses the ramdisk at `initrd_addr`.
- `/dev`: devfs nodes for `console`, `tty0`, `ttyS0`, `null`, `zero`.
- pipefs: 4 KiB ring buffers, blocking readers/writers, `EPIPE` on closed read
  end, `dup2` support for the shell.
- Writes to ext2 return `EROFS`; `/dev` and pipes are writable.

**Scheduler / process (`kernel/sched`, `kernel/exec`)**
- `struct task`: pid/ppid, state, trap frame pointer, 16 KiB kernel stack,
  address space, VMA list, fd table (16), cwd, exit status, CPU time, wait queue.
- Preemptive round-robin at 100 Hz; 5-tick timeslice; idle task executes `hlt`.
- Context switch: save callee-saved registers + MXCSR/XMM state, swap kernel
  stacks, restore, `iretq` from the new trap frame.
- `fork`: COW address-space clone, fd duplication, child returns 0.
- `execve`: VFS-read ELF64, validate, new address space, load PT_LOAD segments
  with correct file/mem sizes, zero BSS, build argv/envp strings and pointer
  array on the new stack, set entry/RSP, replace old space.
- `wait4`/`exit`: zombie reaping, wait queues, orphan reparenting to pid 1.
- `brk`, anonymous `mmap`/`munmap`, demand-zero pages, COW refcounts.

**Syscall ABI**
- `SYSCALL`: `rax` = number, args `rdi, rsi, rdx, r10, r8, r9`, result in
  `rax`; errors as `-errno` (Linux convention). Entry does `swapgs`, switches
  to the per-CPU kernel stack, and builds the same trap frame as the
  interrupt gates; return is `sysretq` with the reordered GDT selectors.
- `int 0x80`: the same register and error convention, retained as a
  compatibility gate. Args are validated and user pointers copied via
  usercopy, which wraps `stac`/`clac` for SMAP.

| # | Syscall | # | Syscall |
|---|---|---|---|
| 0 | `read` | 57 | `fork` |
| 1 | `write` | 59 | `execve` |
| 2 | `open` | 60 | `exit` |
| 3 | `close` | 61 | `wait4` |
| 4/5 | `stat`/`fstat` | 62 | `kill` |
| 8 | `lseek` | 63 | `uname` |
| 9/11 | `mmap`/`munmap` | 83/84 | `chdir`/`getcwd` |
| 12 | `brk` | 217 | `getdents64` |
| 16 | `ioctl` | 35 | `nanosleep` |
| 22 | `pipe` | 39 | `getpid` |
| 32/33 | `dup`/`dup2` | 110 | `getppid` |

Custom debug helpers (not part of the POSIX surface): `sys_procs` (for `ps`),
`sys_reboot`, `sys_poweroff` (ACPI/`isa-debug-exit` in test builds).

**Drivers (`drivers/`)**
- **Serial 16550 COM1**: 115200 8N1, polled TX, IRQ4 RX ring; mirror of kernel
  log and console output; input feeds the active tty for automated tests.
- **PS/2 8042**: controller init, self-test, port enable, IRQ1/IRQ12.
  Keyboard set-1 decoding with E0 prefixes, US keymap, shift/ctrl/alt, LEDs.
  Mouse 3-byte packet sync, sign extension, overflow rejection, acceleration,
  screen clamping, button state.
- **VESA LFB**: mode picked by the bootloader, 32 bpp preferred, 24 bpp
  supported, pitch-aware; double-buffered compositor; fill/rect/line/blit,
  8×16 embedded public-domain VGA font.
- **Console/tty**: VT100 subset (CSI cursor, erase, SGR 16 colors), 1000-line
  scrollback, canonical line discipline, echo, backspace, Ctrl-C line flush.
- **Desktop**: wallpaper, top bar (name, uptime, RTC clock, task count), one
  terminal window with title/close/drag, taskbar, software mouse cursor, dirty
  rectangle redraw at 60 Hz.
- **RTC**: CMOS BCD conversion, wall clock.
- **Ramdisk block device**: read-only ext2 image from the boot protocol.

### 3.7 Userland

```
/            ext2 root, read-only
/init        pid 1: sets cwd, opens console, reaps orphans, spawns /bin/sh
/bin/sh      line editor, quotes, builtins (cd, exit, help, pwd, clear)
/bin/ls /bin/cat /bin/echo /bin/pwd /bin/ps /bin/kill
/bin/sleep /bin/uname /bin/clear /bin/wc /bin/head
/etc/motd
```

* Tiny libc: `crt0`, syscall wrappers, `string`, `stdlib` (`malloc` via `brk`),
  `stdio` (`printf`/`vsnprintf`), `unistd`, `errno`, and `ctype`.
* Static non-PIE ELF at `0x400000`, linked with `linker/user.ld`.
* Shell supports pipelines (`|`), input/output redirection (`<`, `>`), and
  background execution is intentionally omitted.
* GUI is kernel-owned in v1 (no userland window protocol); the shell and all
  programs render into the terminal window.

---

## 4. Directory tree

```
zenku/
├── Makefile
├── config.mk
├── README.md
├── LICENSE
├── .editorconfig
├── .gitignore
├── .github/
│   └── workflows/ci.yml
├── docs/
│   ├── PLAN.md
│   ├── boot-protocol.md
│   └── screenshots/
│       ├── filesystem.png
│       ├── hero.png
│       ├── scheduler.png
│       └── shell.png
├── linker/
│   ├── kernel.ld
│   ├── stage3.ld
│   └── user.ld
├── include/
│   ├── bdev.h
│   ├── boot_info.h
│   ├── boot_offsets.inc
│   ├── compiler.h
│   ├── elf.h
│   ├── errno.h
│   ├── io.h
│   ├── kio.h
│   ├── kpanic.h
│   ├── rtc.h
│   ├── string.h
│   ├── types.h
│   └── version.h
├── boot/
│   ├── stage1/stage1.asm
│   ├── stage2/
│   │   ├── console.asm
│   │   ├── disk.asm
│   │   ├── entry.asm
│   │   ├── gdt.asm
│   │   ├── iso9660.asm
│   │   ├── longmode.asm
│   │   ├── mem.asm
│   │   └── vbe.asm
│   ├── stage3/
│   │   ├── entry.asm
│   │   └── main.c
│   └── uefi/
│       ├── efi.h
│       ├── font8x16.c
│       └── main.c
├── kernel/
│   ├── arch/x86_64/
│   │   ├── cpu.c
│   │   ├── cpu.h
│   │   ├── entry.asm
│   │   ├── gdt.c
│   │   ├── gdt.h
│   │   ├── gdt_flush.asm
│   │   ├── idt.c
│   │   ├── idt.h
│   │   ├── irq.c
│   │   ├── irq.h
│   │   ├── isr.asm
│   │   ├── syscall.asm
│   │   └── user.asm
│   ├── exec/
│   │   ├── loader.c
│   │   └── loader.h
│   ├── fs/
│   │   ├── devfs.c
│   │   ├── devfs.h
│   │   ├── pipe.c
│   │   ├── pipe.h
│   │   ├── ramdisk.c
│   │   ├── ramdisk.h
│   │   ├── vfs.c
│   │   └── vfs.h
│   ├── mm/
│   │   ├── heap.c
│   │   ├── heap.h
│   │   ├── pmm.c
│   │   ├── pmm.h
│   │   ├── usercopy.c
│   │   ├── usercopy.h
│   │   ├── vmm.c
│   │   └── vmm.h
│   ├── sched/
│   │   ├── process.c
│   │   └── process.h
│   ├── syscall/
│   │   ├── syscall.c
│   │   └── syscall.h
│   ├── console.c
│   ├── console.h
│   ├── desktop.c
│   ├── desktop.h
│   ├── fb.c
│   ├── fb.h
│   ├── idle.c
│   ├── idle.h
│   ├── main.c
│   ├── panic.c
│   ├── tty.c
│   └── tty.h
├── drivers/
│   ├── acpi.c
│   ├── acpi.h
│   ├── mouse.c
│   ├── mouse.h
│   ├── pic.c
│   ├── pic.h
│   ├── pit.c
│   ├── pit.h
│   ├── ps2.c
│   ├── ps2.h
│   ├── serial.c
│   ├── serial.h
│   └── time/rtc.c
├── fs/
│   ├── ext2.c
│   └── ext2.h
├── lib/
│   ├── kio.c
│   └── string.c
├── libc/
│   ├── include/
│   │   ├── ctype.h
│   │   ├── errno.h
│   │   ├── stdio.h
│   │   ├── stdlib.h
│   │   ├── string.h
│   │   ├── unistd.h
│   │   └── zenku.h
│   └── src/
│       ├── ctype.c
│       ├── errno.c
│       ├── stdio.c
│       ├── stdlib.c
│       ├── string.c
│       └── unistd.c
├── user/
│   ├── coreutils/
│   │   ├── cat.c
│   │   ├── clear.c
│   │   ├── echo.c
│   │   ├── head.c
│   │   ├── kill.c
│   │   ├── ls.c
│   │   ├── poweroff.c
│   │   ├── ps.c
│   │   ├── pwd.c
│   │   ├── sleep.c
│   │   ├── uname.c
│   │   └── wc.c
│   ├── init/init.c
│   ├── sh/sh.c
│   ├── crt0.asm
│   └── user.ld
├── scripts/
│   ├── build-boot.sh
│   ├── check-layout.sh
│   ├── gdbinit
│   ├── iso-ls.py
│   ├── mkefi.sh
│   ├── mkiso.sh
│   ├── mkrootfs.sh
│   ├── qemu-headless.sh
│   ├── qemu-probe.py
│   ├── run-fault-test.sh
│   ├── run-fs-test.sh
│   ├── run-input-test.sh
│   ├── run-proc-test.sh
│   ├── run-serial-test.sh
│   ├── run-sh-test.sh
│   ├── run-tests.sh
│   ├── run-uefi-test.sh
│   ├── run-user-test.sh
│   ├── syntax-check.sh
│   └── toolchain.sh
├── tests/
│   └── host/
│       ├── Makefile
│       ├── mkimage.sh
│       ├── test.h
│       ├── test_main.c
│       ├── test_string.c
│       └── test_ext2.c
└── third_party/
    └── font/
        ├── OFL.txt
        └── README.md
```

---

## 5. Build system

### 5.1 Toolchain

`scripts/toolchain.sh` builds a pinned `x86_64-elf` binutils + GCC into
`$HOME/.local/cross`, with prerequisite checks (`gmp`, `mpfr`, `libmpc`,
`bison`, `flex`, `texinfo`). It is optional:

* `Makefile` sets `CROSS ?= $(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo x86_64-elf- || true)`.
* If `CROSS` is empty, it builds with host `gcc`/`clang` using strict
  freestanding flags. Both paths are tested by `make test`, so the repository
  works on the host immediately and on a proper cross setup later.

### 5.2 Compiler/linker flags (kernel)

```
-std=gnu11 -O2 -g -Wall -Wextra -Werror=return-type
-ffreestanding -fno-pie -fno-pic -fno-stack-protector
-fno-stack-clash-protection -fno-builtin -fno-omit-frame-pointer
-mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 -mno-avx
-Iinclude -Ikernel -Idrivers -Ifs
```

* SSE is disabled in kernel code but enabled for userland; `fxsave` preserves
  user state.
* No libgcc dependency: `scripts/check-layout.sh` fails the build on undefined
  helper symbols (`__udivdi3`, etc.).
* 32-bit stage3: `-m32 -march=i686 -ffreestanding -fno-pic -fno-stack-protector
  -mno-sse -mno-mmx -mno-80387 -fno-builtin`; no 64-bit division helpers.

### 5.3 Makefile targets

| Target | Result |
|---|---|
| `make` / `make all` | `build/os.iso` |
| `make toolchain` | bootstraps `x86_64-elf` (optional) |
| `make boot` | `build/boot/stage1.bin` (stage1+stage2), `build/boot/STAGE3.BIN` |
| `make kernel` | `build/kernel.elf` |
| `make userland` | ELF binaries + `libc.a` |
| `make rootfs` | `build/rootfs.ext2` (16 MiB, populated) |
| `make iso` | `build/os.iso` via xorriso |
| `make uefi` | `build/uefi/BOOTX64.EFI` |
| `make run` | QEMU graphical boot |
| `make debug` | QEMU `-s -S` + GDB script |
| `make test` | headless boot + integration tests |
| `make test-disk` | storage persistence and failure-path boots |
| `make test-host` | native string/mem, ext2, and writer unit tests |
| `make size` | size report per component |
| `make layout` | asserts linker symbols, stage sizes, no overlaps |
| `make clean` / `distclean` | remove `build/` / also generated toolchain artifacts |

Artifacts: `build/obj/<subsystem>/…`, `build/bin/`, `build/rootfs/`,
`build/rootfs.ext2`, `build/efi.img`, `build/os.iso`.

### 5.4 Root filesystem image

```sh
dd if=/dev/zero of=build/rootfs.ext2 bs=1M count=16 status=none
mke2fs -q -t ext2 -F -b 1024 -I 128 -m 0 \
       -L ZENKU_ROOT -O ^64bit,^metadata_csum \
       -d build/rootfs build/rootfs.ext2
```

`build/rootfs/` is populated from `user/` outputs plus `/etc/motd`,
`/kernel`, and the directory skeleton, including an empty `/data` mount
point. `mke2fs -d` requires no root privileges; verified e2fsprogs 1.47.4 is
installed.

### 5.5 ISO assembly

```sh
xorriso -as mkisofs \
  -r -J -V ZENKU \
  -b boot/stage1.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
  -eltorito-alt-boot -e efi.img -no-emul-boot \
  -o build/os.iso build/iso
```

`build/efi.img` is a FAT16 image containing `EFI/BOOT/BOOTX64.EFI` and a copy
of `ROOT.EXT2`. The BIOS entry and the UEFI entry share the same ISO and the
same kernel image.

### 5.6 Run / debug

```sh
# required convenience command
qemu-system-x86_64 -cdrom build/os.iso -m 2G -serial stdio

# extras exposed as variables
make run QEMU_FLAGS="-d int -no-reboot"
make debug
make test
```

`make run` also passes `-vga std` (VBE LFB), `-machine pc` (PS/2 + VBE),
`-no-reboot`, and `-no-shutdown` so triple faults are visible rather than
rebooting the guest.

---

## 6. Phase plan

Each phase ends with a bootable ISO and a concrete acceptance check. No phase
leaves the tree unbuildable.

### Phase 0 — Wipe, skeleton, toolchain, first boot (≈1 session) — COMPLETE

Delivered: 2 KiB El Torito stage1, COM1 banner, BIOS output, `xorriso` ISO,
host-toolchain fallback, `make test` with deterministic `isa-debug-exit`
status 33, layout validation, and the cross-toolchain bootstrap script.

1. Back up any existing working tree, including its `.git` history.
2. Start from an empty working directory.
3. `git init`, commit empty skeleton, add `.gitignore`, README, license,
   `config.mk`, root Makefile.
4. Add `scripts/toolchain.sh`, host-fallback detection, `scripts/mkiso.sh`.
5. Write stage1 that initializes COM1 and prints `ZENKU STAGE1 OK` over serial.
6. Build boot image + ISO, boot in QEMU with `-serial stdio`.
7. Copy this plan to `docs/PLAN.md` and commit it.

**Accept:** `make` produces `os.iso`; serial shows the stage1 banner; QEMU exits
cleanly with `make test` (stage1 test build writes `isa-debug-exit`).

### Phase 1 — Complete boot chain and higher-half kernel (≈3–4 sessions) — COMPLETE

* stage1: boot-info-table LBA, DAP retries, sector-size detection, checksum.
* stage2: A20, unreal mode, E820, VBE mode selection, ISO9660 reader, load
  `ROOT.EXT2` and `STAGE3.BIN`, boot params, diagnostics.
* stage3: ext2 core (shared), ELF64 parse/load, BSS zero, page tables, long
  mode trampoline, boot_info handoff.
* kernel: `_start`, GDT/TSS/IDT stubs, serial + framebuffer text, banner,
  E820/fb/initrd dump, self-test markers, physical-map setup.

**Accept:** `make run` shows the Zenku banner in a graphical window and on
serial; logs list E820 regions, VBE mode, initrd location, kernel high address;
a deliberate ELF corruption produces a clean stage3 error, not a triple fault.

### Phase 2 — CPU, memory management, interrupts (≈2 sessions) — COMPLETE

* GDT/TSS finalization, IDT + all exception stubs, panic with backtrace.
* PIC remap, PIT 100 Hz, IRQ framework, RTC.
* PMM bitmap + reservations + `struct page`; slab heap; VMM map/unmap/protect;
  direct map; guard pages; VMA skeleton.
* Page-fault handler for demand-zero and diagnostics.
* Boot self-tests: PMM alloc/free/leak, map/translate/unmap, heap stress.

**Accept:** intentional `#PF`, `#GP`, `#UD`, divide-by-zero each produce a
readable panic; `make test` reports `ALL TESTS PASSED`; no frame leaks after
10k alloc/free cycles.

### Phase 3 — Drivers, console, desktop skeleton (≈2 sessions) — COMPLETE

* Robust serial driver (IRQ4 RX, TX ring), log tags. **Done:** polled TX and
  IRQ4 RX into the keyboard queue; TX ring and log tags still open.
* PS/2 controller init, keyboard decoding (shift/ctrl/alt, E0, repeat), mouse
  packet decoding, IRQ1/IRQ12. **Done:** keyboard, packet decode, IRQ12, and a
  software cursor; repeat and click actions still open.
* Framebuffer double buffer, draw primitives, font, VT100 console, scrollback,
  line discipline, tty layer. **Done:** draw primitives, font, double buffer,
  console with 256-line scrollback and PgUp/PgDn, plus a tty line discipline
  with cursor editing and command history, plus ANSI SGR colors. Open:
  cursor addressing and the user-space tty layer.
* Desktop skeleton: wallpaper, top bar, terminal window, mouse cursor, clock.
  **Done:** wallpaper, top bar, terminal window, cursor, ticking clock.
* Serial input mirrors keyboard input into the active tty.

**Accept:** typing appears in the graphical terminal and over serial; mouse
moves/clicks; ANSI colors and scrolling work; `make test` can send serial text
and observe echoed output.

**Verified:** `make test-input` (PS/2 edit + run), `make test-serial` (COM1
IRQ4), mouse move/click focus, scrollback, ANSI SGR, and `fetch`. Cursor
addressing is deferred polish.

### Phase 4 — VFS, ext2, ring 3, core syscalls (≈3 sessions) — COMPLETE

* VFS + path resolution + fd table + ramdisk block device + ext2 read-only.
  **Done:** ramdisk, ext2 mount shared with stage3, path lookup, inode stat,
  directory iteration, `ls`/`cat`/`stat`, and a 32-slot fd table with
  `open`/`read`/`write`/`lseek`/`close`/`fstat` returning negative errnos.
* Devfs (`console`, `tty0`, `ttyS0`, `null`, `zero`), pipefs.
  **Done:** devfs with a `devtest` command, plus a non-blocking pipefs
  (`SYS_pipe`, 4 KiB rings, `EAGAIN`/`EPIPE`/EOF) exercised by `/init`.
* ELF loader in kernel, user address spaces, TSS/RSP0, ring-3 entry.
  **Done:** ELF64 ELF loader, user page mappings, TSS rsp0 syscall stack,
  `iretq` to ring 3, and a saved-context exit back to the kernel. A shared
  kernel PML4 is used for now; per-process address spaces land with the
  scheduler in Phase 5.
* `int 0x80` dispatch, usercopy with fixups, syscalls: `open`, `read`, `write`,
  `close`, `exit`, `lseek`, `fstat`, `getpid`, `getppid`.
  **Done:** all listed syscalls plus bounds-checked user copies (unmapped
  pointers return `EFAULT`). Precise exception fixups remain open.
* Spawn `/init` from ext2; init prints files and exits.
  **Done:** `/init` is built into the rootfs, reports `cs 0x1b`, reads
  `/etc/motd`, and exits; the shell survives and can re-run it via `init`.

**Accept:** `/init` runs in ring 3, `cat` of `/etc/motd` works, bad user
pointers return `-EFAULT`, writing to ext2 returns `EROFS`, and a userland
`exit` reaps cleanly.

**Verified:** `make test-user` reports `init: cs 0x1b`, syscall reads of
`/etc/motd`, `EFAULT` on an unmapped pointer, pipe transfer/EAGAIN/EOF,
`EROFS` on ext2 write, and a clean `exit` back to the shell.

### Phase 5 — Scheduler, fork/exec/wait, libc, shell (≈4 sessions) — COMPLETE

* [x] Task/process model, round-robin preemptive scheduler, return-to-kernel
  idle path.
* [x] Eager-copy `fork`, `execve` with argv, blocking `wait4`, orphan
  reparenting, per-process fd tables.
* [x] COW `fork` with page refcounts; timer-based sleep queues and `kill`
  (SIGKILL/SIGTERM) with wait-status delivery.
* [x] `brk`, anonymous `mmap`, `munmap`, demand-zero heap pages, and guarded
  downward stack growth.
* [x] Shared `crt0`, user ABI header; `init`, `echo`.
* [x] Tiny libc + `sh`, `ls`, `cat`, `pwd`, `ps`, `kill`, `sleep`, `uname`,
  `clear`, `wc`, `head`.
  **Done:** a shared libc provides `string`, `stdlib`, `stdio`, `unistd`,
  `errno`, and `ctype`; all utilities and a standalone `pwd` link against it.
* [x] Shell pipelines, redirection, builtins, external exec via `PATH=/bin`.
  External exec through `/bin`, line editing, blocking console reads, up to
  four-stage pipelines (`pipe` + `dup2`), `<`/`>` redirection, per-process
  working directories, and `cd`/`clear`/`exit`/`help`/`pwd` builtins work.
* [x] `make test-proc`: fork/exec/wait, fd inheritance, timer preemption.
* [x] `make test-sh`: shell builtins, external commands, and clean exit.
* [x] Stress tests: 100 sequential forks, COW write separation, fork bomb
  capped by a process limit, exec failure rollback, heap/mmap/stack checks,
  and SIGSEGV-style isolation of user page faults.

**Accept:** interactive shell runs `ls /`, `cat /etc/motd`, `echo hi | wc`,
`ps` shows init/sh/ps, Ctrl-D exits, and the system remains responsive after
stress.

### Phase 6 — Graphical desktop (≈2 sessions) — COMPLETE

* [x] Terminal emulator window with ANSI subset, selection/focus not required.
  SGR colors, `ESC[2J` clear, `ESC[K` erase-to-end-of-line, scrollback, and
  prompt cursor are supported.
* [x] Window chrome: title, close/reopen, drag with mouse, taskbar.
  The title bar drags the window, the bottom-right grip resizes it, the close
  button hides it, and the taskbar button restores it with history intact.
* [x] Top bar: OS name, uptime, RTC clock, running task count, memory usage.
* [x] Cursor sprite, click handling, 60 Hz dirty-rect compositor, redraw on input.
  `fb_present()` copies only the damaged rectangle and the idle loop limits
  full frames to 60 Hz while bypassing the limiter for input.
* [x] Boot splash that transitions into the desktop console.
  The splash is presented, held briefly, and replaced by the desktop window.

**Accept:** desktop is usable purely with mouse + keyboard; resizing/ANSI
content stays correct; no tearing or cursor trails in QEMU; 60 Hz redraw keeps
the shell responsive.

### Phase 7 — UEFI boot path (≈2 sessions) — COMPLETE

* `BOOTX64.EFI` built with `clang --target=x86_64-pc-win32` + `lld-link`
  using vendored minimal EFI headers (no gnu-efi). Implemented in
  `boot/uefi/efi.h` and `boot/uefi/main.c`.
* GOP framebuffer discovery, UEFI memory map, `SimpleFileSystem` to read
  `ROOT.EXT2` and load `/kernel` through the shared ext2/ELF64 core,
  `ExitBootServices`.
* Page tables and the same kernel entry with the same `boot_info` ABI;
  `BOOT_FLAG_UEFI` marks the firmware path, EFI descriptors are translated
  into merged E820 entries, and the PML4 template is copied to the fixed
  `VMM_PML4_PHYS` address.
* FAT16 `efi.img` + dual El Torito entries in `os.iso`, with a 16 MiB root
  filesystem shared by both boot paths.
* `make uefi` builds the loader; `make test-uefi` boots the ISO with OVMF.

**Accept:** `qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd
-cdrom build/os.iso -m 2G` reaches the same kernel banner and userland;
SeaBIOS boot still works from the same ISO. **Met:** both firmware paths
pass the automated boot tests and reach the desktop.

### Phase 8 — Hardening, extras, release (≈2 sessions) — COMPLETE

* [x] `SYSCALL`/`SYSRET` fast path in `kernel/arch/x86_64/syscall.asm`, with a
  per-CPU GS block (`swapgs`), the same `struct regs` frame used by the
  interrupt gates, and `int 0x80` retained as a compatibility gate. The libc
  syscall wrapper uses the fast path.
* [x] CPUID-gated NX, SMEP, and SMAP; `stac`/`clac` around validated user
  copies. CPUs without a feature still boot, with that feature disabled.
* [x] W^X user mappings: executable pages are read-only, writable pages are
  non-executable, and writable+executable ELF segments or `mmap` requests are
  rejected.
* [x] Heap redzones with alloc/free poisoning and integrity checks; guarded
  kernel stacks; panic output mirrored to COM1.
* [x] ext2 edge cases: 1/2/4 KiB blocks, double/triple indirect, deep paths,
  large directories, `..` resolution, and corrupt `rec_len`/name-length
  rejection, exercised by `make test-host`.
* [x] ACPI FADT parse for graceful poweroff through S5, with FADT reset,
  8042 reset, PCI reset, and halt fallbacks; `poweroff` coreutil and
  `SYS_poweroff`.
* [x] Documentation, the `v0.1.0` tag, and the static, host, QEMU, and UEFI
  test matrix.
* Deferred: ATA PIO/ATAPI block device and booting from a raw ext2 second
  disk. The RAM-resident root image remains the supported path.

**Accept:** `make test` and `make test-host` pass; SMEP/SMAP cannot be bypassed
by a bad usercopy test; both BIOS and UEFI ISO boots work; docs match behavior.
**Met:** 208 host checks pass, the usercopy tests reject kernel pointers,
writes to user text, and writable-executable mappings, and both firmware paths
reach the same kernel and userland.

---

### Phase 9 — Persistent storage (≈3 sessions) — COMPLETE

* [x] PCI enumeration and a BAR helper, followed by an AHCI driver with port
  reset/start, IDENTIFY, LBA48 DMA reads and writes, and FLUSH CACHE EXT.
* [x] A legacy PATA driver covering primary/secondary master/slave, LBA28 and
  LBA48 PIO reads and writes, and cache flush.
* [x] MBR and GPT partition parsing with CRC32 validation and a whole-disk
  fallback for unpartitioned media.
* [x] A kernel-only ext2 writer (`fs/ext2_write.c`) for create, write, append,
  truncate, unlink, mkdir, and rmdir, including single- and double-indirect
  blocks and directory growth. The shared reader stays allocation-free.
* [x] A VFS mount table with longest-prefix routing. `/` remains read-only;
  a volume labeled `ZENKU_DATA` mounts read-write at `/data`. Unlabeled ext2
  volumes mount read-only, and the kernel never auto-formats.
* [x] Additive syscalls 170–175 (`mkdir`, `rmdir`, `unlink`, `truncate`,
  `fsync`, `sync`) and matching libc wrappers; `O_CREAT`, `O_EXCL`,
  `O_TRUNC`, and `O_APPEND` in `open`.
* [x] Coreutils `mkdir`, `rm`, `touch`, and `sync`; shell `>` and `>>`
  redirection.
* [x] Host writer tests (496 checks) plus `e2fsck -fn`, and QEMU persistence
  tests for AHCI, PATA, and GPT, with failure paths for unlabeled, corrupt
  MBR/GPT, recovery-pending, full, and missing disks.

**Accept:** `make test-host`, `make test-disk-ahci`, `make test-disk-pata`,
and `make test-disk-faults` pass; a file written under `/data` survives a
reboot; the read-only root and both firmware boot paths are unchanged.
**Met:** two-boot QEMU tests pass on AHCI, PATA, and GPT; corrupted and
recovery-pending volumes are rejected or mounted read-only; `make check`,
`make test`, `make test-uefi`, `make test-fs`, and `make test-sh` stay green.

---

## 7. Test strategy

**Host unit tests (`make test-host`)**
- `tests/host/test_string.c`: `memset`, `memcpy`, `memmove`, `memcmp`,
  `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strchr`, `strrchr`,
  and `strstr` normal, boundary, and overlap cases.
- `tests/host/test_ext2.c` against generated fixture images: superblock and
  root directory, nested files, deep paths, a 200-entry directory, 1/2/4 KiB
  blocks, single/double/triple indirect blocks up to 66 MiB, `..` resolution,
  unsupported extents, bad magic, and corrupt `rec_len`/name-length rejection.

**QEMU integration (`make test`)**
- Headless: `-display none -serial file:build/test-serial.log`
  `-device isa-debug-exit,iobase=0x501,iosize=0x04 -no-reboot`.
- Kernel self-tests behind `CONFIG_SELFTEST`, final marker `ALL TESTS PASSED`.
- Userland tests piped over serial (`printf 'ls\ncat /etc/motd\nexit\n' | …`),
  shell output and shutdown marker checked by `scripts/run-tests.sh`.
- Failure artifacts: serial log, `-d int,cpu_reset` log, QEMU screendump.

**Boot matrix**
- SeaBIOS, `-m 512M` and `-m 2G`, `-smp 1`.
- OVMF UEFI, same ISO.
- Cold boot, warm reboot via `reboot`, and an intentionally corrupt ext2 image
  to verify clean failure paths.
- Optional real-hardware test checklist in `docs/testing.md`.

**Static checks**
- `scripts/check-layout.sh`: kernel LMA/VMA symbols, stage sizes, ISO extents,
  no undefined libgcc symbols, no overlapping regions.
- `-Werror` on project sources (toggle `WERROR=0` for compiler upgrades).

---

## 8. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| El Torito LBA/sector-size differences across firmware | Boot failure on some BIOSes | Boot Info Table, `INT13 AH=48h` geometry probe, retries, 512-byte translation, `ISO_EMUL=hd` fallback |
| 16-bit stage2 complexity (unreal mode, >1 MiB reads) | Corruption/hangs | stage2 limited to BIOS calls + copies + ISO9660; all parsing in 32-bit stage3; checksums and serial diagnostics |
| VBE mode unavailable | No graphics | Preference list, fallback 640×480×32/24, VGA text emergency console, framebuffer optional in `boot_info` |
| ext2 feature mismatch | Root mount failure | Generate conservative ext2 (`^64bit,^metadata_csum`), reject incompatible features with clear errors, host tests |
| COW/fork bugs | Data corruption | Refcounted `struct page`, write-protect both sides, COW stress test, eager-copy fallback compile flag |
| Kernel stack overflow | Panic/corruption | 16 KiB stacks, guard pages, shallow path depth, stack-usage warnings |
| Userland pointer abuse | Kernel crash | VMA validation + exception-table fixups, SMEP/SMAP, NX |
| Ramdisk consumes RAM | 512 MiB machines tight | 16 MiB image, minimum 256 MiB, later ATA direct mount |
| Host vs cross compiler differences | Build breaks | CI builds both; `WERROR` toggle; documented versions |
| Scope | Incomplete release | Phase gates with bootable acceptance, no phase skipped |

---

## 9. Spec traceability

| Requirement | Where |
|---|---|
| Custom bootloader, BIOS/UEFI | `boot/stage1/stage1.asm`, `boot/stage2/*`, `boot/stage3/*`, `boot/uefi/main.c` |
| Explicit memory map parser | `boot/stage2/mem.asm`, kernel PMM |
| Read-only ext2 driver in bootloader/kernel | `fs/ext2.c` shared by stage3, UEFI loader, and kernel; `kernel/fs/ramdisk.c` |
| ELF64 parser, GDT, long mode, paging handoff | `boot/stage3/main.c`, `boot/stage2/gdt.asm`, `boot/stage2/longmode.asm` |
| Higher-half kernel at `0xFFFFFFFF80000000` | `linker/kernel.ld`, stage3/UEFI page tables, `kernel/mm/vmm.c` |
| Frame allocator + paging | `kernel/mm/pmm.c`, `kernel/mm/vmm.c` |
| IDT, exceptions, hardware IRQs, PIT | `kernel/arch/x86_64/idt.c`, `isr.asm`, `irq.c`, `drivers/pit.c` |
| PS/2 keyboard + mouse | `drivers/ps2.c`, `drivers/mouse.c` |
| VESA LFB + graphical desktop | `kernel/fb.c`, `boot/stage2/vbe.asm`, `kernel/desktop.c` |
| Serial COM1 logging | `drivers/serial.c`, used from stage1 onward |
| Preemptive round-robin scheduler | `kernel/sched/process.c`, `kernel/arch/x86_64/isr.asm` frame return |
| User ELF from ext2 | `fs/ext2.c`, `kernel/exec/loader.c`, `kernel/sched/process.c` |
| POSIX core syscalls | `kernel/syscall/syscall.c`, `libc/include/zenku.h` |
| NX/SMEP/SMAP, `SYSCALL`/`SYSRET`, W^X, usercopy | `kernel/arch/x86_64/cpu.c`, `kernel/arch/x86_64/syscall.asm`, `kernel/mm/usercopy.c`, `kernel/mm/vmm.c`, `kernel/mm/heap.c` |
| ACPI poweroff/reboot | `drivers/acpi.c`, `user/coreutils/poweroff.c` |
| Directory structure | Section 4 |
| Master Makefile + `x86_64-elf` + host fallback | Section 5, root `Makefile` |
| `xorriso` ISO packaging | `scripts/mkiso.sh` |
| `make run` QEMU graphical | root `Makefile` |

