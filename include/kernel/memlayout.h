#pragma once
#include <stdint.h>
#include "mmu.h"

#define MAXVA      (1ULL << 38)

#define TRAMPOLINE (MAXVA - PGSIZE)

#define TRAPFRAME  (TRAMPOLINE - PGSIZE)
