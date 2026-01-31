#pragma once
#include <stdint.h>
#include "mmu.h"

typedef uint64_t pte_t;
typedef pte_t* pagetable_t;

#define PTE_V (1UL<<0)
#define PTE_R (1UL<<1)
#define PTE_W (1UL<<2)
#define PTE_X (1UL<<3)
#define PTE_U (1UL<<4)
#define PTE_A (1UL<<6)
#define PTE_D (1UL<<7)

#define PXSHIFT(level) (12 + 9*(level))
#define PX(level, va) (((va) >> PXSHIFT(level)) & 0x1FF)
#define PA2PTE(pa) ((((uint64_t)(pa)) >> 12) << 10)
#define PTE2PA(pte) ((((uint64_t)(pte)) >> 10) << 12)
#define SATP_SV39 (8UL << 60) //user sv39 translation
#define MAKE_SATP(root_pa) (SATP_SV39 | (((uint64_t)(root_pa)) >> 12))

static inline void sfence_vma(void){ asm volatile("sfence.vma zero, zero"); }