# Zenku build configuration
# Shared by the top-level Makefile and, later, the subsystem makefiles.

PROJECT       := zenku
OS_NAME       := Zenku
OS_CODENAME   := Raijin
OS_VERSION    := 0.0.1
ISO_LABEL     := ZENKU

BUILD         := build

# ---------------------------------------------------------------------------
# Toolchain detection
#
# Prefer a real x86_64-elf cross compiler when it is on PATH. Otherwise fall
# back to the host compiler with freestanding flags. Override explicitly with:
#     make CROSS=x86_64-elf-
# or  make CROSS=            (force host fallback)
# ---------------------------------------------------------------------------
CROSS ?= $(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo x86_64-elf- || true)

ifeq ($(strip $(CROSS)),)
  CC             := gcc
  LD             := ld
  OBJCOPY        := objcopy
  TOOLCHAIN_KIND := host-fallback
else
  CC             := $(CROSS)gcc
  LD             := $(CROSS)ld
  OBJCOPY        := $(CROSS)objcopy
  TOOLCHAIN_KIND := cross
endif

NASM      ?= nasm
XORRISO   ?= xorriso
QEMU      ?= qemu-system-x86_64
MKE2FS    ?= mke2fs
DD        ?= dd
MTOOLS    ?= mformat

# Kernel compiler flags (used from Phase 1 onward)
KERNEL_CFLAGS := \
  -std=gnu11 -O2 -g -Wall -Wextra -Werror=return-type \
  -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
  -fno-stack-clash-protection -fno-builtin -fno-omit-frame-pointer \
  -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 -mno-avx \
  -Iinclude -Ikernel -Idrivers -Ifs

# 32-bit bootloader-stage compiler flags (used from Phase 1 onward)
STAGE3_CFLAGS := \
  -std=gnu11 -O2 -g -Wall -Wextra \
  -m32 -march=i686 -ffreestanding -fno-pie -fno-pic \
  -fno-stack-protector -fno-builtin -mno-sse -mno-mmx -mno-80387 \
  -Iinclude -Iboot/stage3

# User-space compiler flags (used from Phase 5 onward)
USER_CFLAGS := \
  -std=gnu11 -O2 -g -Wall -Wextra \
  -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
  -Ilibc/include

# QEMU
QEMU_MEMORY   ?= 2G
QEMU_COMMON   := -m $(QEMU_MEMORY) -machine pc -vga std -no-reboot -no-shutdown
QEMU_FLAGS    ?=
