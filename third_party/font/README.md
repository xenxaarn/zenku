# Console font

`boot/uefi/font8x16.c` contains a 256-glyph, 8x16 bitmap console font used by
the UEFI boot path. The glyphs were rasterized from **Liberation Mono**
13 px at a 96/255 coverage threshold; codes outside printable ASCII are left
blank. The same 8x16 cell size is used by the BIOS boot path, which copies the
firmware's VGA font instead.

Liberation Mono is distributed under the SIL Open Font License 1.1. The full
license text is in `OFL.txt`. The rasterized table is a derived work and is
redistributed here under the same license.
