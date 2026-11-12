SHELL := /bin/bash
.DEFAULT_GOAL := all

include config.mk

STAGE1_SRC  := boot/stage1/stage1.asm
STAGE2_SRC  := boot/stage2/entry.asm
STAGE2_BIN  := $(BUILD)/boot/stage2.bin
STAGE2_TEST := $(BUILD)/test/stage2.bin
STAGE3_SRC  := boot/stage3/entry.asm
STAGE3_BIN  := $(BUILD)/boot/stage3.bin
STAGE3_TEST := $(BUILD)/test/stage3.bin
BOOT_IMAGE  := $(BUILD)/boot/boot.img
TEST_IMAGE  := $(BUILD)/test/boot.img
ISO         := $(BUILD)/os.iso
TEST_ISO    := $(BUILD)/test/os.iso

STAGE2_DEPS := $(wildcard boot/stage2/*.asm) include/boot_offsets.inc

.PHONY: all iso run debug test clean distclean doctor size layout layout-test help boot toolchain

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
	$(NASM) -f bin -I. -o $@ $(STAGE2_SRC)

$(STAGE2_TEST): $(STAGE2_DEPS) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -I. -dTEST_EXIT -o $@ $(STAGE2_SRC)

$(STAGE3_BIN): $(STAGE3_SRC) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -Ox -o $@ $(STAGE3_SRC)

$(STAGE3_TEST): $(STAGE3_SRC) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -Ox -dTEST_EXIT -o $@ $(STAGE3_SRC)

$(BOOT_IMAGE): $(STAGE1_SRC) $(STAGE2_BIN) scripts/build-boot.sh | $(BUILD)/dirs
	scripts/build-boot.sh $(STAGE1_SRC) $(STAGE2_BIN) $@

$(TEST_IMAGE): $(STAGE1_SRC) $(STAGE2_TEST) scripts/build-boot.sh | $(BUILD)/dirs
	scripts/build-boot.sh $(STAGE1_SRC) $(STAGE2_TEST) $@ -dTEST_EXIT

# ---------------------------------------------------------------------------
# ISO images
# ---------------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(BOOT_IMAGE) $(STAGE3_BIN) scripts/mkiso.sh
	scripts/mkiso.sh $(BOOT_IMAGE) $(ISO) $(STAGE3_BIN)

$(TEST_ISO): $(TEST_IMAGE) $(STAGE3_TEST) scripts/mkiso.sh
	scripts/mkiso.sh $(TEST_IMAGE) $(TEST_ISO) $(STAGE3_TEST)

# ---------------------------------------------------------------------------
# Run / debug / test
# ---------------------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $(ISO) $(QEMU_COMMON) -serial stdio $(QEMU_FLAGS)

debug: $(ISO)
	$(QEMU) -cdrom $(ISO) $(QEMU_COMMON) -serial stdio -s -S $(QEMU_FLAGS)

test: $(TEST_ISO)
	QEMU=$(QEMU) scripts/run-tests.sh $(TEST_ISO)

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
	@for tool in $(NASM) $(XORRISO) $(QEMU) $(CC) $(LD) stat; do \
		if command -v $$tool >/dev/null 2>&1; then \
			printf '  %-10s %s\n' "$$tool" "$$(command -v $$tool)"; \
		else \
			printf '  %-10s MISSING\n' "$$tool"; \
		fi; \
	done

size: $(BOOT_IMAGE)
	@stat -c '%n: %s bytes' $(BOOT_IMAGE) $(STAGE2_BIN)

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
	@echo "  toolchain  bootstrap an x86_64-elf cross toolchain"
	@echo "  doctor     show toolchain and tool availability"
	@echo "  size       report boot image size"
	@echo "  layout     validate the boot image layout"
	@echo "  clean      remove build outputs"
