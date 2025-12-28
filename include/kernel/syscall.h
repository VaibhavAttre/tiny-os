#pragma once
#include <stdint.h>

struct trapframe;

enum {
    SYSCALL_PUTC = 1,
    SYSCALL_YIELD = 2,
    SYSCALL_TICKS = 3,
    SYSCALL_SLEEP = 4,
    SYSCALL_GETPID = 5,
    SYSCALL_EXIT = 6,
};

void syscall_handler(struct trapframe * tf);