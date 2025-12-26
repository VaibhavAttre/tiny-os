.DEFAULT_GOAL := all

RISCV_PREFIX  ?= riscv64-unknown-elf-

RISCV_CC      := $(RISCV_PREFIX)gcc
RISCV_LD      := $(RISCV_PREFIX)ld
RISCV_OBJCOPY := $(RISCV_PREFIX)objcopy

BUILD := build

CFLAGS  := -g -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
           -march=rv64imac -mabi=lp64 -mcmodel=medany \
           -Iinclude

LDFLAGS := -T linker.ld -nostdlib

# -----------------------------
# User test: src/user/test.S -> build/user_test.bin -> include/user_test.h
# -----------------------------
USER_ASM := src/user/test.S
USER_ELF := $(BUILD)/user_test.elf
USER_BIN := $(BUILD)/user_test.bin
USER_HDR := include/user_test.h

# Only ONE build rule
$(BUILD):
	mkdir -p $(BUILD)

$(USER_ELF): $(USER_ASM) | $(BUILD)
	$(RISCV_CC) -nostdlib -nostartfiles -ffreestanding \
	  -march=rv64imac -mabi=lp64 \
	  -Wl,-Ttext=0 -Wl,-e,_start \
	  -o $@ $<

$(USER_BIN): $(USER_ELF)
	$(RISCV_OBJCOPY) -O binary $< $@

# Your xxd does NOT support -n, so we rename via sed instead
$(USER_HDR): $(USER_BIN)
	@echo "Generating $@ from $<"
	@{ \
	  echo "#pragma once"; \
	  xxd -i $< | sed -e 's/build_user_test_bin/user_test_bin/g' \
	               -e 's/build_user_test_bin_len/user_test_bin_len/g'; \
	} > $@

# -----------------------------
# Kernel build
# -----------------------------
OBJS := \
	$(BUILD)/boot.o \
	$(BUILD)/kernel.o \
	$(BUILD)/uart.o \
	$(BUILD)/panic.o \
	$(BUILD)/printf.o \
	$(BUILD)/ktrap.o \
	$(BUILD)/trap.o \
	$(BUILD)/mtrap.o \
	$(BUILD)/timer.o \
	$(BUILD)/clock.o \
	$(BUILD)/sched.o \
	$(BUILD)/kalloc.o \
	$(BUILD)/vm.o \
	$(BUILD)/swtch.o \
	$(BUILD)/syscall.o \
	$(BUILD)/kernelvec.o \
	$(BUILD)/trampoline.o

all: $(USER_HDR) kernel.elf

$(BUILD)/boot.o: src/arch/riscv/boot.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: src/kernel/kernel.c $(USER_HDR) | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart.o: src/drivers/uart.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/panic.o: src/kernel/panic.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/printf.o: src/kernel/printf.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/trap.o: src/kernel/trap.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ktrap.o: src/arch/riscv/ktrap.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mtrap.o: src/arch/riscv/mtrap.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/timer.o: src/kernel/timer.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/clock.o: src/kernel/clock.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sched.o: src/kernel/sched.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kalloc.o: src/kernel/kalloc.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vm.o: src/kernel/vm.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/swtch.o: src/arch/riscv/swtch.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscall.o: src/kernel/syscall.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernelvec.o: src/arch/riscv/kernelvec.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/trampoline.o: src/arch/riscv/trampoline.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(RISCV_LD) $(LDFLAGS) -o $@ $(OBJS)

run: kernel.elf
	qemu-system-riscv64 \
	  -machine virt \
	  -smp 1 \
	  -bios none \
	  -kernel kernel.elf \
	  -nographic

clean:
	rm -rf $(BUILD)
	rm -f kernel.elf
	rm -f $(USER_HDR)

.PHONY: all run clean
