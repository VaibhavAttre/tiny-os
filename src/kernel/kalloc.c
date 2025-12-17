#include <stdint.h>
#include "mmu.h"
#include "kernel/kalloc.h"
#include "kernel/panic.h"

extern char __stack_top[];

struct run {struct run * next;};
static struct {struct run * freelist;} kernel_mem;

/*
Free list:
Free list implementation of k malloc system. Uses explicit free list (for efficency reasons)
Each free block is structured like this:
[next ptr][rest of free page 4096 bytes] => run
*/

static void freerange(uint64_t start, uint64_t end) {

    uint64_t page = PGRUP(start);
    for(; page + PGSIZE <= end; page += PGSIZE) {

        kfree((void*)page);
    }
}

void kfree(void* p) {

    uint64_t page = (uint64_t) p;
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