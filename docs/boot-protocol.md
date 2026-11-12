# Zenku Boot Protocol

This document describes the boot media, the stage1 layout, and the
`boot_info` handoff. Phase 0 implements stage1; later phases extend the same
contract.

## Media

`build/os.iso` is an ISO9660 image with an El Torito **no-emulation** BIOS boot
entry. The boot image is `boot/stage1.bin`, a 2048-byte file:

| Offset | Size | Contents |
|---|---|---|
| `0x0000` | 2 | `jmp short entry` (`EB 3E`) |
| `0x0002` | 1 | NOP |
| `0x0003` | 5 | zero padding |
| `0x0008` | 56 | El Torito Boot Info Table (patched by `xorriso`) |
| `0x0040` | up to 446 | stage1 code and data |
| `0x01FE` | 2 | boot signature `0xAA55` |
| `0x0200` | 1536 | zero padding to 2048 bytes |

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

## Stage1 behavior (Phase 0)

1. Set up segments and a stack at `0000:7C00`.
2. Save `DL`.
3. Initialize COM1 at 115200 8N1 (DLAB divisor 1, FIFO on, DTR/RTS).
4. Print `ZENKU STAGE1 OK` and `boot drive: 0xNN` to COM1.
5. Print the banner through BIOS `INT 10h, AH=0Eh`.
6. Wait for the UART transmitter to drain.
7. Test builds write `0x10` to port `0x501` (`isa-debug-exit`, QEMU status 33);
   production builds halt with interrupts disabled.

## ISO layout

```
/boot/stage1.bin     El Torito boot image (2048 bytes + stage2 from Phase 1)
/STAGE3.BIN          32-bit loader (Phase 1)
/ROOT.EXT2           32 MiB ext2 root filesystem (Phase 4)
/README.TXT
/boot.catalog        El Torito catalog (generated)
```

## Planned physical layout (from Phase 1)

| Range | Use |
|---|---|
| `0x00000000–0x000004FF` | IVT / BIOS data area |
| `0x00001000–0x00004FFF` | stage3 page tables |
| `0x00005000–0x00005FFF` | E820 map |
| `0x00006000–0x00006FFF` | `boot_params` (stage2 → stage3) |
| `0x00007C00–0x000083FF` | stage1 |
| `0x00009000–0x00018FFF` | stage2 |
| `0x00020000–0x0009FFFF` | stage3 |
| `0x000A0000–0x000FFFFF` | VGA/ROM hole |
| `0x00100000–…` | kernel physical image |
| `0x04000000–0x05FFFFFF` | ext2 root image |
| LFB physical base | VBE framebuffer |

## `boot_info` ABI

Defined in `include/boot_info.h` (Phase 1). Magic `0x5A454E4B55303031`
(`"ZENKU001"`), version 1, packed struct. The stage3 loader passes its
**physical** address in `RDI`; the kernel copies it after enabling the direct
map and validates magic/version before use.

## Test protocol

`make test` builds a stage1 variant with `-dTEST_EXIT`. Successful serial output:

```
ZENKU STAGE1 OK
boot drive: 0xE0
```

QEMU is started with:

```
-device isa-debug-exit,iobase=0x501,iosize=0x04
```

Stage1 writes `0x10`, so QEMU exits with status `(0x10 << 1) | 1 = 33`, which
`scripts/run-tests.sh` treats as success.
