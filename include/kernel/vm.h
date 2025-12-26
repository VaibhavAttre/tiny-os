#pragma once
#include <stdint.h>
#include "sv39.h"

void kvminit(void);
void kvmenable(void);

pagetable_t kvmpagetable(void);
pagetable_t uvmcreate(void);

int vm_map(pagetable_t pt, uint64_t va, uint64_t pa, uint64_t size, int perm);
void vm_switch(pagetable_t pt);

void dump_pte(pagetable_t pt, uint64_t va);