#pragma once
#include <stdint.h>

struct extent {
    uint32_t start;
    uint32_t len;
};

// Extent allocator using the B-tree free-space map (prototype).
void extent_init(void);
int extent_alloc(uint32_t len, struct extent *out);
void extent_free(uint32_t start, uint32_t len);
int extent_commit(void);
