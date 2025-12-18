#include <stdint.h>
#include "mmu.h"
#include "sv39.h"
#include "kernel/kalloc.h"
#include "kernel/panic.h"
#include "kernel/printf.h"

static pagetable_t kpt;

extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];
extern char __bss_start[], __bss_end[];
extern char __stack_bottom[], __stack_top[];
extern char _end[];

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

static void kmap_range(uint64_t a, uint64_t b, uint64_t perm) {

    a = PGRDOWN(a);
    b = PGRUP(b);
    if (b <= a) return;
    //dir map for now
    if(mappages(kpt, a, a, b- a, perm) < 0) panic("kvminit");
}

/*
DEBUG FUNC
*/
static void dump_pte(pagetable_t pt, uint64_t va) {
    pte_t *pte = walk(pt, va, 0);
    if (!pte) { kprintf("va %p: no pte\n", (void*)va); return; }
    kprintf("va %p: pte=%p pa=%p flags=%p\n",
            (void*)va,
            (void*)(*pte),
            (void*)PTE2PA(*pte),
            (void*)(*pte & 0x3FF));
}

void kvminit(void) {

    kpt = (pagetable_t)kalloc();
    if(!kpt) panic("kvminit no mem err");

    kmap_range((uint64_t)__text_start, (uint64_t)__text_end, PTE_R|PTE_X|PTE_A);
    kmap_range((uint64_t)__rodata_start, (uint64_t)__rodata_end, PTE_R|PTE_A);
    kmap_range((uint64_t)__data_start, (uint64_t)__data_end, PTE_R|PTE_W|PTE_A|PTE_D);
    kmap_range((uint64_t)__bss_start, (uint64_t)__bss_end, PTE_R|PTE_W|PTE_A|PTE_D);
    kmap_range((uint64_t)__stack_bottom, (uint64_t)__stack_top, PTE_R|PTE_W|PTE_A|PTE_D);

    //kprintf("reached A");
    //unused as RW (such as heap)
    uint64_t start = PGRUP((uint64_t)_end);
    uint64_t end = RAM_BASE + RAM_SIZE;
    if(start < end) {
        kmap_range(start, end, PTE_R|PTE_W|PTE_A|PTE_D);
    }

    //kprintf("reached B");
    //UART
    kmap_range(0x10000000UL, 0x10000000UL + PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D);
    //DEBUG
    dump_pte(kpt, (uint64_t)__text_start);
    dump_pte(kpt, (uint64_t)__rodata_start);
    dump_pte(kpt, (uint64_t)__data_start);
    dump_pte(kpt, 0x10000000UL);
    dump_pte(kpt, 0x0);  
}

void kvmenable(void) {

    //kprintf("Reached C");
    sfence_vma();
    
    //kprintf("Reached D");
    w_satp(MAKE_SATP((uint64_t)kpt));
    
    //kprintf("Reached E");
    sfence_vma();

}