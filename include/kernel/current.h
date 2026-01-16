#pragma once
#include <stdint.h>
#include "kernel/sched.h"

static inline uint64_t read_sp() {

    uint64_t x;
    asm volatile("mv %0, sp": "=r"(x));
    return x;
}

static inline struct proc * myproc() {
    struct proc *p = getmyproc();
    if (p) return p;

    uint64_t sp = read_sp();
    uint64_t base = sp & ~(KSTACK_SIZE - 1);
    return *(struct proc **)base;
}
