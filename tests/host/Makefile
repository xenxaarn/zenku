# Host unit tests for shared kernel code (string/mem and ext2).
#
#   make          build and run the suite
#   make run      same, but assumes the binary is up to date
#   make clean    remove the test binary and generated images
#
# Fixtures are generated into $(BUILD)/images by mkimage.sh; they are not
# tracked by git.

CC ?= cc

ROOT := ../..
BUILD := $(ROOT)/build/host-tests
IMAGES := $(BUILD)/images
BIN := $(BUILD)/test_host

# -fno-builtin matches the freestanding kernel build: without it GCC may
# replace the project's own memset/memcpy loops with calls to themselves.
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror -fno-builtin \
          -I$(ROOT)/include -I$(ROOT)/fs

SRCS := test_main.c test_string.c test_ext2.c test_ext2_write.c \
        $(ROOT)/lib/string.c $(ROOT)/fs/ext2.c $(ROOT)/fs/ext2_write.c

.PHONY: all run clean images

all: run

$(BIN): $(SRCS) test.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(IMAGES)/.stamp: mkimage.sh
	@mkdir -p $(IMAGES)
	./mkimage.sh $(IMAGES)
	@touch $@

images: $(IMAGES)/.stamp

run: $(BIN) $(IMAGES)/.stamp
	$(BIN) $(IMAGES)

clean:
	rm -rf $(BUILD)
