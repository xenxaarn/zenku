# Zenku Boot Protocol

This document describes the boot media, the three-stage boot chain, and the
`boot_info` handoff. Phase 1 implements the complete chain through the
higher-half kernel.

## Media

`build/os.iso` is an ISO9660 image with an El Torito **no-emulation** BIOS boot
entry. The boot image `boot/stage1.bin` starts with a 2048-byte stage1 sector
followed by the raw stage2 payload (padded to a 2048-byte boundary):

| Offset | Size | Contents |
|---|---|---|
| `0x0000` | 2 | `jmp short entry` (`EB 3E`) |
| `0x0002` | 1 | NOP |
| `0x0003` | 5 | zero padding |
| `0x0008` | 56 | El Torito Boot Info Table (patched by `xorriso`) |
| `0x0040` | up to 446 | stage1 code and data |
| `0x01FE` | 2 | boot signature `0xAA55` |
| `0x0200` | 1536 | zero padding of the stage1 sector |
| `0x0800` | stage2 size | stage2 flat binary (`ZST2` header), patched size |

Stage1 is loaded by the BIOS at `0000:7C00`. The BIOS boot drive is reported in
`DL`.

The Boot Info Table is written by `xorriso -boot-info-table` inside the ISO:

| Offset | Field | Current value |
|---|---|---|
| `+0` | `bi_pvd` — LBA of the primary volume descriptor | 16 |
| `+4` | `bi_file` — LBA of this boot image | 34 |
| `+8` | `bi_length` — boot image size in bytes | 2048 |
| `+12` | `bi_csum` — 32-bit checksum | non-zero |

The raw `build/boot/stage1.bin` keeps offsets 8..63 zero; `scripts/check-layout.sh`
enforces that. Phase 1 uses `bi_file` to locate stage2 inside the boot-image
extent without hard-coding an ISO LBA.

## Stage1 behavior

1. Set up segments and a stack at `0000:7C00`.
2. Save `DL`.
3. Initialize COM1 at 115200 8N1 (DLAB divisor 1, FIFO on, DTR/RTS).
4. Print `ZENKU STAGE1 OK` and `boot drive: 0xNN` to COM1.
5. Print the banner through BIOS `INT 10h, AH=0Eh`.
6. Wait for the UART transmitter to drain.
7. Load stage2 from the El Torito extent using the boot info table LBA.
8. Test builds write `0x10` to port `0x501` (`isa-debug-exit`, QEMU status 33);
   production builds halt with interrupts disabled.

## Stage2 behavior

1. A20 and unreal mode; E820 collection into 24-byte ACPI 3.0 entries.
2. Copy the BIOS 8x16 font (INT 43h vector) to `0x70000`.
3. Mount ISO9660, load `STAGE3.BIN` at `0x20000` and `ROOT.EXT2` at
   `0x04000000`, and record their locations in `boot_params`.
4. Select a 1024x768x32 linear-framebuffer VBE mode and record the pitch,
   dimensions, and channel position/size fields.
5. Enter long mode with the first 1 GiB identity-mapped and jump to stage3.

## Stage3 behavior

1. Mount the ext2 image through the shared read-only ext2 core.
2. Read `/kernel` and validate the ELF64 header and program headers.
3. Load each `PT_LOAD` segment into `0x00100000..0x03FFFFFF`, zeroing BSS.
4. Build page tables at `0x1000`: 1 GiB identity map (reused), 4 GiB direct
   map at `0xFFFF800000000000`, 512 MiB kernel map at `0xFFFFFFFF80000000`.
5. Fill `boot_info` at `0x7000` and jump to the ELF entry with the physical
   `boot_info` address in `RDI`.

## ISO layout

```
/boot/stage1.bin     El Torito boot image (stage1 + stage2)
/STAGE3.BIN          64-bit ext2/ELF loader
/ROOT.EXT2           32 MiB ext2 root filesystem containing /kernel
/README.TXT
/boot.catalog        El Torito catalog (generated)
```

## Physical layout

| Range | Use |
|---|---|
| `0x00000000–0x000004FF` | IVT / BIOS data area |
| `0x00001000–0x00004FFF` | stage3 page tables |
| `0x00005000–0x00005FFF` | stage2 E820 map (24-byte entries) |
| `0x00006000–0x00006FFF` | `boot_params` (stage2 → stage3) |
| `0x00007000–0x00007FFF` | `boot_info` (stage3 → kernel) |
| `0x00007C00–0x000083FF` | stage1 |
| `0x00008400–0x00008FFF` | normalized E820 entries (up to 128) |
| `0x00009000–0x0001FFFF` | stage2 |
| `0x00020000–0x0005FFFF` | stage3 |
| `0x00070000–0x00070FFF` | BIOS 8x16 font copy |
| `0x000A0000–0x000FFFFF` | VGA/ROM hole |
| `0x00100000–0x03FFFFFF` | kernel physical image window |
| `0x04000000–0x05FFFFFF` | ext2 root image |
| LFB physical base | VBE framebuffer |

## `boot_info` ABI

Defined in `include/boot_info.h`. Magic `0x5A454E4B55303031` (`"ZENKU001"`),
version 1, packed struct. The stage3 loader passes its **physical** address in
`RDI`; the kernel converts it through the direct map
(`0xFFFF800000000000 + phys`) and validates magic/version before use. Flags
report framebuffer (`1`), initrd (`2`), and font (`4`) availability.

## Test protocol

`make test` builds a stage1 variant with `-dTEST_EXIT` and a kernel variant with
`-DTEST_EXIT`. The kernel writes `0x10` to `isa-debug-exit` after the splash,
so QEMU exits with status `(0x10 << 1) | 1 = 33`. `scripts/run-tests.sh`
asserts the stage1/stage2/stage3 banners, the ext2 and ELF load lines, the
kernel banner, the framebuffer message, and the idle-loop message.

QEMU is started with:

```
-device isa-debug-exit,iobase=0x501,iosize=0x04
```

A failing kernel panic writes `0x20` (status 65); stage3 fatal errors write
codes `0x21–0x25`.
