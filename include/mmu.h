#pragma once
#include <stdint.h>

#define PGSIZE 4096
#define PGRDOWN(x) ((x) & ~(PGSIZE-1))
#define PGRUP(x) (((x) + PGSIZE-1) & ~(PGSIZE-1))
#define RAM_BASE 0x80000000UL
#define RAM_SIZE (128UL * 1024 * 1024)
#define RAM_END (RAM_BASE + RAM_SIZE)