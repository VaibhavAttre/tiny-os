#pragma once
#include <stdint.h>

#define CLINT_BASE 0x02000000UL

//mtimecmp is an array per hart
//64 bit compare register
//starts at 0x4000 so:
//  hart0: CLINTBASE + 0x4000*hartnum

//mtime is a 64 bit counter increasing at a fixed rate
#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000UL + 8UL * (hart)) 
#define CLINT_MTIME (CLINT_BASE + 0xBFF8UL)

//helpers
//use somthing like:
//  x = mmio_read64(CLINT_MTIME)
//  mmio_write(CLINT_MTIMECMP(hart), x + dx)
static inline uint64_t mmio_read64(uint64_t addr) {
    return *(volatile uint64_t*)addr;
}

static inline void mmio_write64(uint64_t addr, uint64_t x) {
    *(volatile uint64_t*)addr = x;
}