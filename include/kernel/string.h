#pragma once
#include <stdint.h>

static inline void memzero(void *dst, uint64_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (uint64_t i = 0; i < n; i++) d[i] = 0;
}

static inline void memcopy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}
