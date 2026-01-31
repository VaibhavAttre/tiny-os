#pragma once
#include <stdint.h>

#define CLINT_BASE 0x02000000UL

#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000UL + 8UL * (hart))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8UL)

static inline uint64_t mmio_read64(uint64_t addr) {
    return *(volatile uint64_t*)addr;
}

static inline void mmio_write64(uint64_t addr, uint64_t x) {
    *(volatile uint64_t*)addr = x;
}