# tiny-os

A tiny RISC-V operating system built from scratch to learn low-level systems, inspired by [RISC-V](https://riscv.org/), [xv6](https://pdos.csail.mit.edu/6.828/2024/xv6.html), and Linux

---

## Background

This is a custom toy OS I’m building to understand how computers really work under the hood.

After building a simple datapath on an FPGA, I got hooked on low-level systems. That project made me want to go deeper into how CPUs, memory, and kernels actually behave.
So I decided to take on the challenge of writing a small OS:

- Runs on **RISC-V** (RV64)  
- Booted and tested via **QEMU**  

---

## Technical Overview

### Software Stack

- **Language:** C (with a bit of assembly for bootstrapping and python for data analysis)
- **Architecture:** RISC-V 64-bit
- **Platform:** Single-core virtual machine under QEMU
- **Toolchain:** `riscv64-unknown-elf-gcc`, `riscv64-unknown-elf-ld`

---

## Roadmap

### Filesystem Status (current)

What exists today in the tree (rough, but accurate):

- **Disk + cache**
  - VirtIO block driver (QEMU `virt`)
  - Buffer cache with `bread`, `bwrite`, `brelse`
  - `mkfs` tool for the on-disk layout
- **On-disk layout (xv6-style + CoW refcounts)**
  - Superblock, bitmap, refcount blocks, inodes, data blocks
  - Direct + single-indirect blocks
- **Inodes + directories**
  - `iget/ilock/iupdate`, `readi/writei`
  - `dirlookup/dirlink`, path walk (`namei`)
- **Syscalls + basic UX**
  - `open/read/write/close`, `mkdir`, `unlink`, `fstat`
  - `chdir/getcwd` (root-only `getcwd` stub)
  - `dup`
- **CoW features**
  - Block-level refcounting + CoW on write
  - `clone(src, dst)` reflink syscall (shares blocks)

This means the current filesystem is functional and CoW-aware at the block layer, but it is not yet the B-tree metadata design described in Phases 8-12.

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
- [x] **Stress Testing**
  - Added stress tests and stats logging
  - Python code to convert log to csv
  - Added functionality for printing statistics over time
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
  - Ability to trace scheduler usage

---

### Phase 6 — Syscalls & Program Loading

- [x] **Syscall Interface**
  - Simple syscall table and dispatcher
  - Safe user memory helpers (`copyin`, `copyout`, `copyinstr`)
- [x] **Program Loader**
  - Load user programs (ELF subset) into memory
  - Set up initial user stack and entry point
  - Current approach: exec-from-embedded-ELF blobs (switch to exec-from-path after FS)

---

### Phase 7 — Storage Stack (VirtIO + Buffer Cache)

Goal: treat disk as a reliable **block device** with caching + writeback. This is required before any “modern CoW FS”.

- [x] **VirtIO Block Driver (QEMU `virt`)**
  - Device discovery + feature negotiation
  - Virtqueue setup
  - Sector `read/write` (polling first; interrupts later)
- [x] **Buffer / Block Cache**
  - `bread`, `bwrite`, `brelse`
  - Dirty tracking + flush path
  - Simple eviction policy
- [ ] **Crash/Stress Harness (for Phase 7)**
  - Write/read pattern tests
  - “kill QEMU mid-write” scripts (expect corruption for now—Phase 8+ fixes this)

Done when: you can repeatedly write blocks, reboot, and verify integrity under stress.

---

### Phase 8 — CoW Metadata Engine (B-tree + Transactions + Checksums)

Goal: implement the modern core: **copy-on-write updates**, **checksummed metadata blocks**, and **atomic commits**.

- [ ] **On-disk metadata block format**
  - Common header: magic, type, logical addr, generation, checksum
  - Verify checksum on read; reject corrupt blocks
- [ ] **Multiple superblocks**
  - 2–3 copies at fixed locations
  - Generation counter to pick newest valid superblock on mount
- [ ] **Generic on-disk B-tree**
  - Search/insert/delete
  - Split/merge/rebalance
  - Stable key ordering + node formats
- [ ] **Copy-on-write path copying**
  - Never overwrite live metadata blocks
  - Modifying a leaf copies that leaf + all ancestors up to the root
- [ ] **Transactions + atomic commit**
  - Allocate new blocks during txn
  - Commit protocol: write new roots → write superblock last
  - Crash safety: after reboot you mount either old or new root (never half)

Done when: you can store key/value items in a B-tree, update them via CoW, and survive crashes with consistent metadata.

---

### Phase 9 — Space Manager + Extents (Allocation in a CoW FS)

Goal: robust allocation in a CoW system (correctness comes before performance).

- [ ] **Extent-based allocator**
  - Allocate contiguous runs where possible
  - Allocate separately for metadata blocks and data extents
- [ ] **Deferred frees**
  - Never free blocks until the committing superblock is durable
  - Replay-safe semantics
- [ ] **Free-space structure**
  - Start with a simple free-space B-tree (or bitmap tree)
  - Add coalescing of neighboring free extents

Done when: you can allocate/free across many transactions with zero leaks and no double-allocations.

---

### Phase 10 — Filesystem Metadata Trees (“btrfs-lite” layout)

Goal: represent a real filesystem using **multiple trees** (minimum viable set).

- [ ] **Root Tree**
  - Maps “subvolume id → FS tree root pointer”
- [ ] **FS Tree**
  - Inode items
  - Directory entries (name → inode)
  - File extent items (file offset → extent)
- [ ] **Extent Tree**
  - Extent refs + allocation bookkeeping
- [ ] **Checksum Tree (optional initially)**
  - Store checksums for data extents (metadata checksums remain mandatory)

Done when: you can `mkdir`, create, path-lookup, and list directories entirely from disk-backed metadata.

---

### Phase 11 — File Data I/O (CoW reads/writes) + FS Syscalls

Goal: make user programs actually read/write **disk-backed files**.

- [x] **CoW file writes (block-level)**
  - CoW on shared blocks via refcounts
  - Overwrite-in-middle does not corrupt other files
- [x] **Read path**
  - Block lookup → disk reads → `copyout` to user
- [x] **Syscalls**
  - `open`, `read`, `write`, `close`
  - `mkdir`, `unlink`, `fstat`, `chdir`, `getcwd`, `dup`
  - `clone` (reflink) syscall

Note: this phase is implemented on top of the simple inode layout (no extents yet).

Done when: a user program can create a file, write it, reboot, read it back, and pass stress tests.

---

### Phase 12 — Snapshots + Reflinks (Modern CoW Features)

Goal: the features that differentiate CoW B-tree systems (APFS/Btrfs/ZFS-style).

- [ ] **Subvolumes**
  - Root tree maps subvol id → FS tree root
- [ ] **Snapshots**
  - Snapshot = new root item pointing at existing FS tree root (cheap, instant)
  - Writable snapshots: CoW handles divergence automatically
- [ ] **Reflinks (file-level clones)**
  - Clone file extents without copying data
  - Break sharing on write

Done when: reflink-copy is instant and modifying the clone doesn’t modify the original.

---

### Phase 13 — Init & Shell

- [ ] **Init Process**
  - First user process started by the kernel
- [ ] **Simple Shell**
  - Run basic user programs
  - Provide a minimal interactive environment

---

### Phase 14 — Heap

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
  - Finish and harden a **modern CoW, B-tree filesystem** (checksums, transactions, snapshots)
  - Add “real system” maintenance features: scrub/verify, defrag, and recovery tooling

- **Optimizations**
  Implement low-level optimizations such as buddy allocators
---

## Next Best Path (to reach the CoW B-tree design)

If the goal is to finish the filesystem as described in Phases 8-12, the most productive path is:

1) **Finish inode lifecycle + truncation**
   - Implement `itrunc()` to drop blocks safely (respecting CoW refcounts).
   - Update `unlink()` to free blocks when `nlink` reaches 0.
   - Add `O_TRUNC` support in `open` and a simple `truncate` syscall.
2) **Crash consistency + tooling**
   - Add a minimal journal or ordered-write discipline for inode/bitmap updates.
   - Add a `fsck`-style checker in `tools/` to validate bitmap/refcounts/inodes.
3) **Metadata integrity**
   - Add checksums for metadata blocks (superblock, inodes, indirects).
   - Add multiple superblocks + generation selection at mount.
4) **B-tree metadata engine (Phase 8)**
   - Standalone on-disk B-tree + CoW path copying.
   - Transaction commit protocol (write new roots → superblock last).
5) **Space manager + extents (Phase 9)**
   - Move data allocation from block pointers to extents.
   - Deferred frees based on transaction generation.
6) **Filesystem trees (Phase 10)**
   - Root tree, FS tree, extent tree (optional checksum tree).
7) **User-facing polish**
   - `rename`, `link`, directory iteration (`readdir`), and full `getcwd`.

### Extra Features (nice-to-have)

- **Online scrub / verify** (walk metadata + data checksums)
- **Snapshots** (cheap subvolume roots)
- **Reflink copy-on-write clones** (already present; extend to dirs later)
- **Simple defrag** (rewrite fragmented extents)
- **Space quotas** (per inode or per subvolume)
- **Compression** (per extent, optional)

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
```
