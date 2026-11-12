SHELL := /bin/bash
.DEFAULT_GOAL := all

include config.mk

STAGE1_SRC  := boot/stage1/stage1.asm
STAGE1_BIN  := $(BUILD)/boot/stage1.bin
TEST_STAGE1 := $(BUILD)/test/stage1.bin
ISO         := $(BUILD)/os.iso
TEST_ISO    := $(BUILD)/test/os.iso

.PHONY: all iso run debug test clean distclean doctor size layout help boot toolchain

all: iso

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
$(BUILD)/dirs:
	@mkdir -p $(BUILD)/boot $(BUILD)/test $(BUILD)/obj $(BUILD)/iso
	@touch $@

# ---------------------------------------------------------------------------
# Bootloader (Phase 0: 2 KiB stage1 only)
# ---------------------------------------------------------------------------
boot: $(STAGE1_BIN)

$(STAGE1_BIN): $(STAGE1_SRC) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -o $@ $<

$(TEST_STAGE1): $(STAGE1_SRC) | $(BUILD)/dirs
	@mkdir -p $(dir $@)
	$(NASM) -f bin -dTEST_EXIT -o $@ $<

# ---------------------------------------------------------------------------
# ISO images
# ---------------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(STAGE1_BIN) scripts/mkiso.sh
	scripts/mkiso.sh $(STAGE1_BIN) $(ISO)

$(TEST_ISO): $(TEST_STAGE1) scripts/mkiso.sh
	scripts/mkiso.sh $(TEST_STAGE1) $(TEST_ISO)

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

size: $(STAGE1_BIN)
	@stat -c '%n: %s bytes' $(STAGE1_BIN)

layout: $(STAGE1_BIN)
	scripts/check-layout.sh $(STAGE1_BIN)

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
