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
pte_t *walkpte(pagetable_t pt, uint64_t va);

int copyin(pagetable_t pt, char *dst, uint64_t srcva, uint64_t len);
int copyout(pagetable_t pt, uint64_t dstva, char *src, uint64_t len);
int copyinstr(pagetable_t pt, char *dst, uint64_t srcva, uint64_t max);
