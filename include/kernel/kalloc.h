#pragma once
#include <stdint.h>

void kinit(void);
void * kalloc(void);
void kfree(void * p);
void * kalloc_n(uint32_t n);
void kfree_n(void * base, uint32_t n);
void * kalloc_aligned_n(uint32_t n, uint64_t align);
