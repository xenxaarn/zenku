SHELL := /bin/bash
.DEFAULT_GOAL := all

include config.mk

STAGE1_SRC  := boot/stage1/stage1.asm
STAGE2_SRC  := boot/stage2/entry.asm
STAGE2_DEPS := $(wildcard boot/stage2/*.asm) include/boot_offsets.inc

STAGE3_CSRCS := boot/stage3/main.c fs/ext2.c lib/string.c lib/kio.c
STAGE3_ASMS  := boot/stage3/entry.asm
STAGE3_LD    := linker/stage3.ld

KERNEL_CSRCS := kernel/main.c kernel/fb.c kernel/panic.c \
                kernel/console.c kernel/desktop.c kernel/tty.c \
                kernel/arch/x86_64/gdt.c kernel/arch/x86_64/idt.c \
                kernel/arch/x86_64/irq.c \
                kernel/mm/pmm.c kernel/mm/vmm.c kernel/mm/heap.c \
                kernel/fs/ramdisk.c kernel/fs/vfs.c kernel/fs/devfs.c fs/ext2.c \
                kernel/exec/user.c kernel/syscall/syscall.c \
                drivers/pic.c drivers/pit.c drivers/ps2.c drivers/mouse.c drivers/serial.c \
                lib/string.c lib/kio.c
KERNEL_ASMS  := kernel/arch/x86_64/entry.asm \
                kernel/arch/x86_64/gdt_flush.asm \
                kernel/arch/x86_64/user.asm \
                kernel/arch/x86_64/isr.asm
KERNEL_LD    := linker/kernel.ld

STAGE2_BIN      := $(BUILD)/boot/stage2.bin
STAGE2_TEST     := $(BUILD)/test/stage2.bin
STAGE3_ELF      := $(BUILD)/stage3/stage3.elf
STAGE3_BIN      := $(BUILD)/boot/stage3.bin
BOOT_IMAGE      := $(BUILD)/boot/boot.img
TEST_IMAGE      := $(BUILD)/test/boot.img
KERNEL_ELF      := $(BUILD)/kernel.elf
KERNEL_TEST_ELF := $(BUILD)/test/kernel.elf
USER_INIT_ELF   := $(BUILD)/user/init.elf
ROOTFS          := $(BUILD)/rootfs.ext2
ROOTFS_TEST     := $(BUILD)/test/rootfs.ext2
ISO             := $(BUILD)/os.iso
TEST_ISO        := $(BUILD)/test/os.iso

STAGE3_OBJS := $(patsubst %.c,$(BUILD)/stage3/obj/%.o,$(STAGE3_CSRCS)) \
               $(patsubst %.asm,$(BUILD)/stage3/obj/%.o,$(STAGE3_ASMS))
KERNEL_OBJS := $(patsubst %.c,$(BUILD)/kernel/obj/%.o,$(KERNEL_CSRCS)) \
               $(patsubst %.asm,$(BUILD)/kernel/obj/%.o,$(KERNEL_ASMS))
KERNEL_TEST_OBJS := $(patsubst %.c,$(BUILD)/kernel-test/obj/%.o,$(KERNEL_CSRCS)) \
                    $(patsubst %.asm,$(BUILD)/kernel-test/obj/%.o,$(KERNEL_ASMS))

.PHONY: all iso run debug test test-input test-serial test-fs test-user check check-link test-faults fault-test clean distclean doctor size layout layout-test help boot toolchain stage3 kernel rootfs

all: iso

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
$(BUILD)/dirs:
	@mkdir -p $(BUILD)/boot $(BUILD)/test $(BUILD)/obj $(BUILD)/iso
	@touch $@

# ---------------------------------------------------------------------------
# Bootloader: 2 KiB stage1 + stage2 payload
# ---------------------------------------------------------------------------
boot: $(BOOT_IMAGE)

$(STAGE2_BIN): $(STAGE2_DEPS) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -Ox -I. -o $@ $(STAGE2_SRC)

$(STAGE2_TEST): $(STAGE2_DEPS) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -Ox -I. -dTEST_EXIT -o $@ $(STAGE2_SRC)

$(BOOT_IMAGE): $(STAGE1_SRC) $(STAGE2_BIN) scripts/build-boot.sh | $(BUILD)/dirs
	scripts/build-boot.sh $(STAGE1_SRC) $(STAGE2_BIN) $@

$(TEST_IMAGE): $(STAGE1_SRC) $(STAGE2_TEST) scripts/build-boot.sh | $(BUILD)/dirs
	scripts/build-boot.sh $(STAGE1_SRC) $(STAGE2_TEST) $@ -dTEST_EXIT

# ---------------------------------------------------------------------------
# Stage3: 64-bit C ext2 + ELF64 loader
# ---------------------------------------------------------------------------
stage3: $(STAGE3_BIN)

$(BUILD)/stage3/obj/%.o: %.c | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(CC) $(STAGE3_CFLAGS) -c -o $@ $<

$(BUILD)/stage3/obj/%.o: %.asm | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -o $@ $<

$(STAGE3_ELF): $(STAGE3_OBJS) $(STAGE3_LD)
	$(LD) -m elf_x86_64 -T $(STAGE3_LD) -o $@ $(STAGE3_OBJS)

$(STAGE3_BIN): $(STAGE3_ELF)
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c%s $@); limit=$$((0x40000)); \
	if [ $$size -gt $$limit ]; then \
		echo "error: stage3.bin is $$size bytes (limit $$limit)" >&2; \
		exit 1; \
	fi

# ---------------------------------------------------------------------------
# Kernel: higher half at 0xFFFFFFFF80000000, LMA 0x00200000
# ---------------------------------------------------------------------------
kernel: $(KERNEL_ELF)

$(BUILD)/kernel/obj/%.o: %.c | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

$(BUILD)/kernel/obj/%.o: %.asm | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -o $@ $<

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_LD)
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=4096 -T $(KERNEL_LD) -o $@ $(KERNEL_OBJS)

$(BUILD)/kernel-test/obj/%.o: %.c | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -DTEST_EXIT -c -o $@ $<

$(BUILD)/kernel-test/obj/%.o: %.asm | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -DTEST_EXIT -o $@ $<

$(KERNEL_TEST_ELF): $(KERNEL_TEST_OBJS) $(KERNEL_LD)
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=4096 -T $(KERNEL_LD) -o $@ $(KERNEL_TEST_OBJS)

# ---------------------------------------------------------------------------
# Fault-injection kernels: one build per exception vector, used by
# `make test-faults` to prove the IDT/panic path is readable.
# ---------------------------------------------------------------------------
FAULT_N   ?= 0
FAULT_DIR := $(BUILD)/fault/$(FAULT_N)
FAULT_OBJS := $(patsubst %.c,$(FAULT_DIR)/obj/%.o,$(KERNEL_CSRCS)) \
              $(patsubst %.asm,$(FAULT_DIR)/obj/%.o,$(KERNEL_ASMS))
FAULT_KERNEL := $(FAULT_DIR)/kernel.elf
FAULT_ROOTFS := $(FAULT_DIR)/rootfs.ext2
FAULT_ISO    := $(FAULT_DIR)/os.iso

$(FAULT_DIR)/obj/%.o: %.c | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -DFAULT_TEST=$(FAULT_N) -c -o $@ $<

$(FAULT_DIR)/obj/%.o: %.asm | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -DFAULT_TEST=$(FAULT_N) -o $@ $<

$(FAULT_KERNEL): $(FAULT_OBJS) $(KERNEL_LD)
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=4096 -T $(KERNEL_LD) -o $@ $(FAULT_OBJS)

$(FAULT_ROOTFS): $(FAULT_KERNEL) scripts/mkrootfs.sh | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	scripts/mkrootfs.sh $(FAULT_KERNEL) $@

$(FAULT_ISO): $(BOOT_IMAGE) $(STAGE3_BIN) $(FAULT_ROOTFS) scripts/mkiso.sh
	scripts/mkiso.sh $(BOOT_IMAGE) $@ $(STAGE3_BIN) $(FAULT_ROOTFS)

fault-test: $(FAULT_ISO)
	QEMU=$(QEMU) scripts/run-fault-test.sh $(FAULT_ISO) $(FAULT_N)

test-faults:
	@set -e; for n in 0 1 2 3; do \
		$(MAKE) --no-print-directory fault-test FAULT_N=$$n; \
	done

# ---------------------------------------------------------------------------
# Root filesystem images
# ---------------------------------------------------------------------------
rootfs: $(ROOTFS)

$(BUILD)/user/obj/%.o: %.c | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(USER_INIT_ELF): $(BUILD)/user/obj/user/init/init.o user/user.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=4096 -T user/user.ld -o $@ $<

$(ROOTFS): $(KERNEL_ELF) $(USER_INIT_ELF) scripts/mkrootfs.sh | $(BUILD)/dirs
	scripts/mkrootfs.sh $(KERNEL_ELF) $@ 32 $(USER_INIT_ELF)

$(ROOTFS_TEST): $(KERNEL_TEST_ELF) scripts/mkrootfs.sh | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	scripts/mkrootfs.sh $(KERNEL_TEST_ELF) $@

# ---------------------------------------------------------------------------
# ISO images
# ---------------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(BOOT_IMAGE) $(STAGE3_BIN) $(ROOTFS) scripts/mkiso.sh
	scripts/mkiso.sh $(BOOT_IMAGE) $(ISO) $(STAGE3_BIN) $(ROOTFS)

$(TEST_ISO): $(TEST_IMAGE) $(STAGE3_BIN) $(ROOTFS_TEST) scripts/mkiso.sh
	scripts/mkiso.sh $(TEST_IMAGE) $(TEST_ISO) $(STAGE3_BIN) $(ROOTFS_TEST)

# ---------------------------------------------------------------------------
# Run / debug / test
# ---------------------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $(ISO) $(QEMU_COMMON) -serial stdio $(QEMU_FLAGS)

debug: $(ISO)
	$(QEMU) -cdrom $(ISO) $(QEMU_COMMON) -serial stdio -s -S $(QEMU_FLAGS)

test: $(TEST_ISO)
	QEMU=$(QEMU) scripts/run-tests.sh $(TEST_ISO)

test-input: $(ISO)
	QEMU=$(QEMU) scripts/run-input-test.sh $(ISO)

test-serial: $(ISO)
	QEMU=$(QEMU) scripts/run-serial-test.sh $(ISO)

test-fs: $(ISO)
	QEMU=$(QEMU) scripts/run-fs-test.sh $(ISO)

test-user: $(ISO)
	QEMU=$(QEMU) scripts/run-user-test.sh $(ISO)

check:
	scripts/syntax-check.sh

check-link:
	scripts/syntax-check.sh --link

# Print a Makefile variable (used by scripts/syntax-check.sh).
print-%:
	@echo '$($*)'

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
toolchain:
	scripts/toolchain.sh

# ---------------------------------------------------------------------------
# Diagnostics and housekeeping
# ---------------------------------------------------------------------------
doctor:
	@echo "Zenku build doctor"
	@echo "  project:   $(PROJECT) $(OS_VERSION) ($(OS_CODENAME))"
	@echo "  toolchain: $(TOOLCHAIN_KIND)"
	@echo "  CC:        $(CC)"
	@echo "  NASM:      $(NASM)"
	@echo "  xorriso:   $(XORRISO)"
	@echo "  QEMU:      $(QEMU)"
	@for tool in $(NASM) $(XORRISO) $(QEMU) $(CC) $(LD) stat $(MKE2FS); do \
		if command -v $$tool >/dev/null 2>&1; then \
			printf '  %-10s %s\n' "$$tool" "$$(command -v $$tool)"; \
		else \
			printf '  %-10s MISSING\n' "$$tool"; \
		fi; \
	done

size: $(BOOT_IMAGE) $(STAGE3_BIN) $(KERNEL_ELF)
	@stat -c '%n: %s bytes' $(BOOT_IMAGE) $(STAGE2_BIN) $(STAGE3_BIN) $(KERNEL_ELF)

layout: $(BOOT_IMAGE)
	scripts/check-layout.sh $(BOOT_IMAGE)

layout-test: $(TEST_IMAGE)
	DEBUG_EXIT=1 scripts/check-layout.sh $(TEST_IMAGE)

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(BUILD)/toolchain-src $(BUILD)/toolchain-build

help:
	@echo "Zenku build targets"
	@echo "  all        build build/os.iso (default)"
	@echo "  run        boot the ISO in QEMU with a graphical display"
	@echo "  debug      boot under QEMU with a GDB stub (-s -S)"
	@echo "  test       headless boot + serial assertions"
	@echo "  test-input boot, type help via PS/2, check the console"
	@echo "  test-serial type help over COM1 IRQ4, check the console"
	@echo "  test-fs    mount ext2, then ls/cat/stat over COM1"
	@echo "  test-user  run /init in ring 3 and check the syscalls"
	@echo "  test-faults inject #DE/#UD/#GP/#PF and check panics"
	@echo "  check      fast syntax check of C/asm/shell/python"
	@echo "  check-link syntax check plus stage3 and kernel links"
	@echo "  boot       build the boot image only"
	@echo "  stage3     build the stage3 loader"
	@echo "  kernel     build the kernel ELF"
	@echo "  rootfs     build the ext2 root filesystem image"
	@echo "  toolchain  bootstrap an x86_64-elf cross toolchain"
	@echo "  doctor     show toolchain and tool availability"
	@echo "  size       report image sizes"
	@echo "  layout     validate the boot image layout"
	@echo "  clean      remove build outputs"
