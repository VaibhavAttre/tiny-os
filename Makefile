RISCV_CC  := riscv64-unknown-elf-gcc
RISCV_LD  := riscv64-unknown-elf-ld

CFLAGS    := -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
             -march=rv64imac -mabi=lp64 -mcmodel=medany \
             -Iinclude

LDFLAGS   := -T linker.ld -nostdlib

BUILD    := build

OBJS := $(BUILD)/boot.o \
		$(BUILD)/kernel.o \
		$(BUILD)/uart.o \
		$(BUILD)/panic.o 

all: kernel.elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/panic.o: src/kernel/panic.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot.o: src/arch/riscv/boot.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart.o: src/drivers/uart.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: src/kernel/kernel.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(RISCV_LD) $(LDFLAGS) -o $@ $(OBJS)


run: kernel.elf
	qemu-system-riscv64 \
	  -machine virt \
	  -bios none \
	  -kernel kernel.elf \
	  -nographic

clean:
	rm -f $(BUILD) kernel.elf

.PHONY: all run clean
