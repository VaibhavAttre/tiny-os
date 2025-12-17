#include <stdint.h>
#include "mmu.h"
#include "sv39.h"
#include "kernel/kalloc.h"
#include "kernel/panic.h"

static pagetable_t kpt;

//3 depth tree tyle page table walk
static pte_t * walk(pagetable_t pt, uint64_t va, int alloc) {

    for(int level =2; level > 0; level--) {
        
        pte_t * pte = &pt[PX(level, va)]; //extract virt PN index for given level
        if(*pte & PTE_V) {
            pt = (pagetable_t)PTE2PA(*pte);
        } else {
            if(!alloc) return 0;
            void* p = kalloc();
            if(!p) return 0;
            *pte = PA2PTE((uint64_t)p) | PTE_V; //go into next level
            pt = (pagetable_t)p;
        }
    }

    return &pt[PX(0, va)]; //final actual page
}

static int mappages(pagetable_t pt, uint64_t va, uint64_t pa, uint64_t sz, uint64_t bitfield) {

    uint64_t a = PGRDOWN(va);
    uint64_t b = PGRDOWN(va + sz - 1);

    for(;;) {
        pte_t * pte = walk(pt, a, 1);
        if(!pte) return -1;
        if(*pte & PTE_V) panic("err");
        *pte = PA2PTE(pa) | bitfield | PTE_V;
        if(a == b) break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

void kvminit(void) {

    kpt = (pagetable_t)kalloc();
    if(!kpt) panic("kvminit no mem err");

    //map all pages as RWX for now

    if(mappages(kpt, RAM_BASE, RAM_BASE, RAM_SIZE, PTE_R|PTE_W|PTE_X|PTE_A|PTE_D) < 0) {
        panic("kvminit mapping err");
    }

    if(mappages(kpt, 0x10000000UL, 0x10000000UL, PGSIZE, PTE_R|PTE_W|PTE_A|PTE_D) < 0) {
        panic("err");
    }

    //map UART to 0x100... wtv it is 
}

void kvmenable(void) {
    sfence_vma();
    w_satp(MAKE_SATP((uint64_t)kpt));
    sfence_vma();
}