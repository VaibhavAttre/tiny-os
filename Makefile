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

USERA_ASM    := src/user/testA.S
USERA_ELF    := $(BUILD)/userA.elf
USERA_BLOB_C := $(BUILD)/userA_blob.c
USERA_BLOB_O := $(BUILD)/userA_blob.o

USERB_ASM    := src/user/testB.S
USERB_ELF    := $(BUILD)/userB.elf
USERB_BLOB_C := $(BUILD)/userB_blob.c
USERB_BLOB_O := $(BUILD)/userB_blob.o

$(BUILD):
	mkdir -p $(BUILD)

$(USERA_ELF): $(USERA_ASM) | $(BUILD)
	$(RISCV_CC) -nostdlib -nostartfiles -ffreestanding \
	  -march=rv64imac -mabi=lp64 \
	  -Wl,-Ttext=0 -Wl,-e,_start \
	  -o $@ $<

$(USERA_BLOB_C): $(USERA_ELF) | $(BUILD)
	@xxd -i $< | sed -e 's/build_userA_elf/userA_elf/g' \
	               -e 's/build_userA_elf_len/userA_elf_len/g' > $@

$(USERA_BLOB_O): $(USERA_BLOB_C) | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@


$(USERB_ELF): $(USERB_ASM) | $(BUILD)
	$(RISCV_CC) -nostdlib -nostartfiles -ffreestanding \
	  -march=rv64imac -mabi=lp64 \
	  -Wl,-Ttext=0 -Wl,-e,_start \
	  -o $@ $<

$(USERB_BLOB_C): $(USERB_ELF) | $(BUILD)
	@xxd -i $< | sed -e 's/build_userB_elf/userB_elf/g' \
	               -e 's/build_userB_elf_len/userB_elf_len/g' > $@

$(USERB_BLOB_O): $(USERB_BLOB_C) | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@


$(USER_HDR): | $(BUILD)
	@echo "Generating $@ (extern declarations)"
	@echo "#pragma once" > $@
	@echo "#include <stdint.h>" >> $@
	@echo "extern const uint8_t user_test_elf[];" >> $@
	@echo "extern const unsigned int user_test_elf_len;" >> $@

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
	$(BUILD)/trampoline.o \
	$(BUILD)/file.o \
	$(BUILD)/virtio_blk.o \
	$(BUILD)/buf.o \
	$(BUILD)/fs.o \
	$(USERA_BLOB_O) \
	$(USERB_BLOB_O) \
	
all: include/user_progs.h $(USERA_BLOB_O) $(USERB_BLOB_O) kernel.elf


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

$(BUILD)/syscall.o: src/kernel/syscall.c $(USER_HDR) | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernelvec.o: src/arch/riscv/kernelvec.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/trampoline.o: src/arch/riscv/trampoline.S | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/file.o: src/kernel/file.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/virtio_blk.o: src/drivers/virtio_blk.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/buf.o: src/kernel/buf.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: src/kernel/fs.c | $(BUILD)
	$(RISCV_CC) $(CFLAGS) -c $< -o $@

# Host tools
MKFS := tools/mkfs

$(MKFS): tools/mkfs.c
	$(CC) -Wall -o $@ $<

kernel.elf: $(OBJS) linker.ld
	$(RISCV_LD) $(LDFLAGS) -o $@ $(OBJS)

DISK := disk.img
DISK_SIZE := 16M
DISK_BLOCKS := 16384

$(DISK): $(MKFS)
	qemu-img create -f raw $(DISK) $(DISK_SIZE)
	./$(MKFS) $(DISK) $(DISK_BLOCKS)

run: kernel.elf $(DISK)
	qemu-system-riscv64 \
	  -machine virt \
	  -smp 1 \
	  -bios none \
	  -kernel kernel.elf \
	  -drive file=$(DISK),if=none,format=raw,id=hd0 \
	  -device virtio-blk-device,drive=hd0 \
	  -nographic

run-nodisk: kernel.elf
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

cleanall: clean
	rm -f $(DISK)

.PHONY: all run run-nodisk clean cleanall
