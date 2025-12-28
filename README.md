# tiny-os

A tiny RISC-V operating system built from scratch to learn low-level systems, inspired by [RISC-V](https://riscv.org/), [xv6](https://pdos.csail.mit.edu/6.828/2024/xv6.html), and Linux

---

## Background

This is a custom toy OS I’m building to understand how computers really work under the hood.

After building a simple datapath on an FPGA, I got hooked on low-level systems. That project made me want to go deeper into how CPUs, memory, and kernels actually behave.
So I decided to take on the challenge of writing a small OS:

- Runs on **RISC-V** (RV64)  
- Booted and tested via **QEMU**  
- Architecturally inspired by **xv6** but implemented from scratch

---

## Technical Overview

### Software Stack

- **Language:** C (with a bit of assembly for bootstrapping)
- **Architecture:** RISC-V 64-bit
- **Platform:** Single-core virtual machine under QEMU
- **Toolchain:** `riscv64-unknown-elf-gcc`, `riscv64-unknown-elf-ld`

---

## Roadmap

### Phase 1 — Core Basics

- [x] **Console I/O**
  - UART driver
  - Minimal `kprintf` implementation
- [x] **Panic Handling**
  - Kernel `panic()` that prints an error and halts
- [x] **Trap & Exception Handling**
  - Set up trap vector
  - Basic fault / exception reporting
  - Set up running in S Mode instead of M mode

---

### Phase 2 — Time & Interrupts

- [x] **Timer Interrupts**
  - Configure machine / supervisor timer
  - Basic tick counter
- [x] **Preemptive Hooks (future)**
  - Use timer as a basis for scheduling later on

---

### Phase 3 — Virtual Memory

- [x] **Page Tables**
  - Set up Sv39 paging
  - Map kernel code, data, stack, and devices
- [x] **Frame Allocator**
  - Simple physical frame allocator
  - Page allocation / freeing APIs

---

### Phase 4 — Processes & Context Switching

- [x] **Process Abstraction**
  - Basic PCB / process struct
- [x] **Context Switching**
  - Save/restore registers
  - Simple round-robin scheduler
- [x] **Sleeping / Waking**
  - Added a sleeping state for procs
  - Added sleep/wakeup functions
---

### Phase 5 — User Mode & Address Spaces

- [x] **User Mode Support**
  - Entering user mode from the kernel
  - Returning to kernel on traps / syscalls
- [x] **Per-Process Address Spaces**
  - Separate virtual address space per process
  - User/kernel memory protection
- [x] **Updated Processes**
  - Added trampoline + trapframe
  - Support for zombies

---

### Phase 6 — Syscalls & Program Loading

- [ ] **Syscall Interface**
  - Simple syscall table and dispatcher
- [ ] **Program Loader**
  - Load user programs (e.g., ELF subset) into memory
  - Set up initial user stack and entry point

---

### Phase 7 — Signals

- [ ] **Signals**
  - Add linux inspired signals
    
---

### Phase 8 — Init & Shell

- [ ] **Init Process**
  - First user process started by the kernel
- [ ] **Simple Shell**
  - Run basic user programs
  - Provide a minimal interactive environment

### Phase 8 — Heap

- [ ] **Custom Malloc + SBRK and heap management**
  - Allow users to access the heap and use malloc, free, etc. 

---

## Future Directions

This is my **first OS project**, so I’m deliberately starting simple:

- **Single core only** for now  
  Later, I’d love to:
  - Add support for **multiple harts/cores**
  - Experiment with **concurrency and synchronization** primitives in the kernel

- **Minimal file system at first**  
  In the future:
  - Design and implement a more **complex file system**
  - Explore caching, journaling, and robustness

- **Optimizations**
  Implement low-level optimizations such as buddy allocators
---

## Goals

- Build intuition about:
  - How a CPU jumps from reset → bootloader → kernel
  - How traps, interrupts, and syscalls actually work
  - How memory management and isolation are implemented
- Use this as a playground for:
  - Experimenting with RISC-V
  - Testing ideas from OS and computer architecture courses
  - Learning to debug low-level issues with GDB and QEMU

---

## Running 

```bash

make all
qemu-system-riscv64 -machine virt -kernel kernel.elf -nographic
