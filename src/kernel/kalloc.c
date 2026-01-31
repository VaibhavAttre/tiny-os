#include <stdint.h>
#include "mmu.h"
#include "kernel/kalloc.h"
#include "kernel/panic.h"
#include "kernel/printf.h"

extern char __stack_top[];

struct run {struct run * next;};
static struct {struct run * freelist;} kernel_mem;

static void freerange(uint64_t start, uint64_t end) {

    uint64_t page = PGRUP(start);
    for(; page + PGSIZE <= end; page += PGSIZE) {

        kfree((void*)page);
    }
}

void kfree(void* p) {

    uint64_t page = (uint64_t) p;
    if (page == 0) {
        kprintf("kfree: ignore null\n");
        return;
    }
    if (page < RAM_BASE || page >= RAM_END) {
        kprintf("kfree: ignore out-of-range %p\n", p);
        return;
    }
    if(page % PGSIZE) panic ("kfree unaligned");

    struct run* r = (struct run*) page;
    r->next = kernel_mem.freelist;
    kernel_mem.freelist = r;
}

void kinit(void) {

    freerange((uint64_t)__stack_top, RAM_END);
}

void * kalloc(void) {

    struct run * r = kernel_mem.freelist;

    if(r) kernel_mem.freelist = r->next;
    if(r) {
        volatile uint8_t* data = (volatile uint8_t*)r;
        for(uint64_t i = 0; i < PGSIZE; i++) data[i] = 0;
    }

    return (void*)r;
}

void * kalloc_n(uint32_t n) {

    if (n == 0) return 0;
    if (n > 64) return 0;

    void *pages[64];
    for (uint32_t i = 0; i < n; i++) pages[i] = 0;

    for (uint32_t i = 0; i < n; i++) {
        pages[i] = kalloc();
        if (!pages[i]) goto fail;
        if (i > 0) {
            uint64_t expect = (uint64_t)pages[0] - (uint64_t)i * PGSIZE;
            if ((uint64_t)pages[i] != expect) goto fail;
        }
    }

    return (void *)((uint64_t)pages[0] - (uint64_t)(n - 1) * PGSIZE);

fail:
    for (uint32_t i = 0; i < n; i++) {
        if (pages[i]) kfree(pages[i]);
    }
    return 0;
}

void kfree_n(void *base, uint32_t n) {
    if (!base || n == 0) return;
    uint8_t *p = (uint8_t *)base;
    for (uint32_t i = 0; i < n; i++) {
        kfree(p + (uint64_t)i * PGSIZE);
    }
}

static int addr_in_set(uint64_t addr, uint64_t *set, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (set[i] == addr) return 1;
    }
    return 0;
}

void * kalloc_aligned_n(uint32_t n, uint64_t align) {
    if (n == 0) return 0;
    if (align < PGSIZE) return 0;
    if (align & (align - 1)) return 0;
    if (align & (PGSIZE - 1)) return 0;
    if (n > 64) return 0;

    for (struct run *r = kernel_mem.freelist; r; r = r->next) {
        uint64_t base = (uint64_t)r;
        if (base & (align - 1)) continue;

        uint64_t addrs[64];
        addrs[0] = base;
        int ok = 1;

        for (uint32_t i = 1; i < n; i++) {
            uint64_t want = base + (uint64_t)i * PGSIZE;
            int found = 0;
            for (struct run *q = kernel_mem.freelist; q; q = q->next) {
                if ((uint64_t)q == want) {
                    found = 1;
                    break;
                }
            }
            if (!found) { ok = 0; break; }
            addrs[i] = want;
        }

        if (!ok) continue;

        struct run *new_head = 0;
        struct run *new_tail = 0;
        for (struct run *q = kernel_mem.freelist; q; ) {
            struct run *next = q->next;
            if (!addr_in_set((uint64_t)q, addrs, n)) {
                if (!new_head) new_head = q;
                else new_tail->next = q;
                new_tail = q;
            }
            q = next;
        }
        if (new_tail) new_tail->next = 0;
        kernel_mem.freelist = new_head;

        for (uint32_t i = 0; i < n; i++) {
            volatile uint8_t *p = (volatile uint8_t *)(addrs[i]);
            for (uint64_t j = 0; j < PGSIZE; j++) p[j] = 0;
        }

        return (void *)base;
    }

    return 0;
}
