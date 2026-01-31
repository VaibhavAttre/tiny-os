#include <stdint.h>
#include "mmu.h"
#include "sv39.h"
#include "kernel/kalloc.h"
#include "kernel/panic.h"
#include "riscv.h"
#include "kernel/printf.h"
#include "kernel/memlayout.h"
#include "kernel/string.h"
#include "kernel/vm.h"

static pagetable_t kpt;

extern char trampoline[];
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];
extern char __bss_start[], __bss_end[];
extern char __stack_bottom[], __stack_top[];
extern char trampoline[], trampoline_end[];
extern char _end[];

static void trampoline_validation() {

    uint64_t t0 = (uint64_t)trampoline;
    uint64_t t1 = (uint64_t)trampoline_end;

    if (t1 - t0 > PGSIZE) {
        panic("trampoline too big");
    }

    if (t0 % PGSIZE != 0) {
        panic("trampoline not page aligned");
    }

    if (t1 < t0) {
        panic("trampoline addresses invalid");
    }
}

static pte_t * walk(pagetable_t pt, uint64_t va, int alloc) {

    for(int level =2; level > 0; level--) {

        pte_t * pte = &pt[PX(level, va)]; //extract virt PN index for given level
        if(*pte & PTE_V) {
            pt = (pagetable_t)PTE2PA(*pte);
        } else {
            if(!alloc) return 0;
            void* p = kalloc();
            if(!p) return 0;
            memzero(p, PGSIZE);
            *pte = PA2PTE((uint64_t)p) | PTE_V; //go into next level
            pt = (pagetable_t)p;
        }
    }

    return &pt[PX(0, va)]; //final actual page
}

static int mappages(pagetable_t pt, uint64_t va, uint64_t pa, uint64_t sz, uint64_t bitfield) {

    if(sz == 0) return 0;

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
    if(mappages(kpt, a, a, b- a, perm) < 0) panic("kvminit");
}

static int map_range(pagetable_t pt, uint64_t a, uint64_t b, uint64_t perm) {

    a = PGRDOWN(a);
    b = PGRUP(b);
    if (b <= a) return 0;
    return mappages(pt, a, a, b - a, perm);
}

void dump_pte(pagetable_t pt, uint64_t va) {
    pte_t *pte = walk(pt, va, 0);
    if (!pte) { kprintf("va %p: no pte\n", (void*)va); return; }
    kprintf("va %p: pte=%p pa=%p flags=%p\n",
            (void*)va,
            (void*)(*pte),
            (void*)PTE2PA(*pte),
            (void*)(*pte & 0x3FF));
}

pte_t *walkpte(pagetable_t pt, uint64_t va) {
    return walk(pt, va, 0);
}

void kvminit(void) {

    trampoline_validation();

    kpt = (pagetable_t)kalloc();
    if(!kpt) panic("kvminit no mem err");
    memzero(kpt, PGSIZE);

    kmap_range((uint64_t)__text_start, (uint64_t)__text_end, PTE_R|PTE_X|PTE_A);
    kmap_range((uint64_t)__rodata_start, (uint64_t)__rodata_end, PTE_R|PTE_A);
    kmap_range((uint64_t)__data_start, (uint64_t)__data_end, PTE_R|PTE_W|PTE_A|PTE_D);
    kmap_range((uint64_t)__bss_start, (uint64_t)__bss_end, PTE_R|PTE_W|PTE_A|PTE_D);
    kmap_range((uint64_t)__stack_bottom, (uint64_t)__stack_top, PTE_R|PTE_W|PTE_A|PTE_D);

    if(mappages(kpt, TRAMPOLINE, (uint64_t)trampoline, PGSIZE, PTE_R | PTE_X | PTE_A) < 0) {
        panic("kvminit trampoline");
    }

    uint64_t start = PGRUP((uint64_t)_end);
    uint64_t end = RAM_BASE + RAM_SIZE;
    if(start < end) {
        kmap_range(start, end, PTE_R|PTE_W|PTE_A|PTE_D);
    }

    kmap_range(0x10000000UL, 0x10000000UL + PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D);

    for (uint64_t addr = 0x10001000UL; addr < 0x10009000UL; addr += PGSIZE) {
        kmap_range(addr, addr + PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D);
    }

    dump_pte(kpt, (uint64_t)__text_start);
    dump_pte(kpt, (uint64_t)__rodata_start);
    dump_pte(kpt, (uint64_t)__data_start);
    dump_pte(kpt, 0x10000000UL);
    dump_pte(kpt, 0x0);
}

void kvmenable(void) {

    sfence_vma();

    w_satp(MAKE_SATP((uint64_t)kpt));

    sfence_vma();
}

void vm_switch(pagetable_t pt) {

    sfence_vma();

    w_satp(MAKE_SATP((uint64_t)pt));

    sfence_vma();
}

pagetable_t kvmpagetable(void) {
    return kpt;
}

pagetable_t uvmcreate(void) {

    trampoline_validation();

    pagetable_t pt = (pagetable_t)kalloc();
    if(!pt) return 0;
    memzero(pt, PGSIZE);

    uint64_t start = (uint64_t)__text_start;
    uint64_t end = (uint64_t)__text_end;
    if(map_range(pt, start, end, PTE_R | PTE_X | PTE_A) < 0) {
        kfree((void*)pt);
        return 0;
    }

    start = (uint64_t)__rodata_start;
    end = (uint64_t)__rodata_end;
    if(map_range(pt, start, end, PTE_R | PTE_A) < 0) {
        kfree((void*)pt);
        return 0;
    }

    start = (uint64_t)__data_start;
    end = (uint64_t)__data_end;
    if(map_range(pt, start, end, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
        kfree((void*)pt);
        return 0;
    }

    start = (uint64_t)__bss_start;
    end = (uint64_t)__bss_end;
    if(map_range(pt, start, end, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
        kfree((void*)pt);
        return 0;
    }

    start = (uint64_t)__stack_bottom;
    end = (uint64_t)__stack_top;
    if(map_range(pt, start, end, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
        kfree((void*)pt);
        return 0;
    }

    if (mappages(pt, TRAMPOLINE, (uint64_t)trampoline, PGSIZE, PTE_R|PTE_X|PTE_A) < 0) {
        kfree((void*)pt);
        return 0;
    }

    if(map_range(pt, 0x10000000UL, 0x10000000UL + PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
        kfree((void*)pt);
        return 0;
    }

    return pt;
}

int vm_map(pagetable_t pt, uint64_t va, uint64_t pa, uint64_t size, int perm) {
    return mappages(pt, va, pa, size, perm);
}

static uint64_t walkaddr(pagetable_t pt, uint64_t va, int check_user) {
    if (va >= MAXVA) return 0;

    pte_t *pte = walk(pt, va, 0);
    if (!pte) return 0;
    if ((*pte & PTE_V) == 0) return 0;
    if (check_user && (*pte & PTE_U) == 0) return 0;

    return PTE2PA(*pte);
}

int copyin(pagetable_t pt, char *dst, uint64_t srcva, uint64_t len) {
    while (len > 0) {
        uint64_t va0 = PGRDOWN(srcva);
        uint64_t pa = walkaddr(pt, va0, 1);
        if (pa == 0) return -1;

        uint64_t off = srcva - va0;
        uint64_t n = PGSIZE - off;
        if (n > len) n = len;

        memcopy(dst, (char*)(pa + off), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

int copyout(pagetable_t pt, uint64_t dstva, char *src, uint64_t len) {
    while (len > 0) {
        uint64_t va0 = PGRDOWN(dstva);
        uint64_t pa = walkaddr(pt, va0, 1);
        if (pa == 0) return -1;

        uint64_t off = dstva - va0;
        uint64_t n = PGSIZE - off;
        if (n > len) n = len;

        memcopy((char*)(pa + off), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

int copyinstr(pagetable_t pt, char *dst, uint64_t srcva, uint64_t max) {
    int got_null = 0;

    while (!got_null && max > 0) {
        uint64_t va0 = PGRDOWN(srcva);
        uint64_t pa = walkaddr(pt, va0, 1);
        if (pa == 0) return -1;

        uint64_t off = srcva - va0;
        uint64_t n = PGSIZE - off;
        if (n > max) n = max;

        char *p = (char*)(pa + off);
        for (uint64_t i = 0; i < n; i++) {
            *dst = *p;
            if (*p == '\0') {
                got_null = 1;
                break;
            }
            dst++;
            p++;
            max--;
        }

        srcva = va0 + PGSIZE;
    }

    if (!got_null) return -1;
    return 0;
}
