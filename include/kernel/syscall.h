#pragma once
#include <stdint.h>

struct trapframe;

// Open flags
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

enum {
    SYSCALL_PUTC = 1,
    SYSCALL_YIELD = 2,
    SYSCALL_TICKS = 3,
    SYSCALL_SLEEP = 4,
    SYSCALL_GETPID = 5,
    SYSCALL_EXIT = 6,
    SYSCALL_EXEC = 7,
    SYSCALL_READ = 8,
    SYSCALL_WRITE = 9,
    SYSCALL_CLOSE = 10,
    SYSCALL_OPEN = 11,
    SYSCALL_CLONE = 12,  // CoW clone (reflink)
    SYSCALL_FORK = 13,
    SYSCALL_WAIT = 14,
    SYSCALL_MKDIR = 15,
    SYSCALL_CHDIR = 16,
    SYSCALL_GETCWD = 17,
    SYSCALL_UNLINK = 18,
    SYSCALL_FSTAT = 19,
    SYSCALL_DUP = 20,
};

void syscall_handler(struct trapframe * tf);