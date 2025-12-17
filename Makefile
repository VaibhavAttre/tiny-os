RISCV_CC  := riscv64-unknown-elf-gcc
RISCV_LD  := riscv64-unknown-elf-ld

CFLAGS    := -g -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
             -march=rv64imac -mabi=lp64 -mcmodel=medany \
             -Iinclude


LDFLAGS   := -T linker.ld -nostdlib

BUILD    := build

OBJS := $(BUILD)/boot.o \
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

$(BUILD)/printf.o: src/kernel/printf.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ktrap.o: src/kernel/trap.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/trap.o: src/arch/riscv/ktrap.S | $(BUILD)
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

kernel.elf: $(OBJS) linker.ld
	$(RISCV_LD) $(LDFLAGS) -o $@ $(OBJS)


run: kernel.elf
	qemu-system-riscv64 \
	  -machine virt \
	  -bios none \
	  -kernel kernel.elf \
	  -nographic \

run-gdb: kernel.elf
	qemu-system-riscv64 \
	  -machine virt \
	  -bios none \
	  -kernel kernel.elf \
	  -nographic \
	  -S -s

clean:
	rm -rf $(BUILD) 
	rm -f kernel.elf

.PHONY: all run clean
