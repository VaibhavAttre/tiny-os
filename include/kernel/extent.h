#pragma once
#include <stdint.h>

struct extent {
    uint32_t start;
    uint32_t len;
};

void extent_init(void);
int extent_alloc(uint32_t len, struct extent *out);
int extent_alloc_meta(uint32_t len, struct extent *out);
int extent_reserve(uint32_t start, uint32_t len);
void extent_free(uint32_t start, uint32_t len);
int extent_commit(void);
int extent_meta_active(void);
