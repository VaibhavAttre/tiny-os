#pragma once
#include <stdint.h>
#include "mmu.h"

typedef uint64_t pte_t;
typedef pte_t* pagetable_t;

/*
page tabel entry:
bits[9:0] are flags:
    Valid, RWx, User, G, A, D...

bits[53:10] = physical page nu mber
    paddr = PPN << 12 + offset (offset are bits[11:0])
*/

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
//remove 12 bit offest to get PPN, then move PPN into PTE's PPN field at bit 10
#define PTE2PA(pte) ((((uint64_t)(pte)) >> 10) << 12)
//pte >> 10 strips off lower 10 bit flags and << 12 turns PPN back to paddr
#define SATP_SV39 (8UL << 60) //user sv39 translation
#define MAKE_SATP(root_pa) (SATP_SV39 | (((uint64_t)(root_pa)) >> 12))

static inline void w_satp(uint64_t x){ asm volatile("csrw satp, %0" :: "r"(x)); }
static inline void sfence_vma(void){ asm volatile("sfence.vma zero, zero"); }