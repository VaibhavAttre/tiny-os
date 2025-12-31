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

// memmove handles overlapping regions
static inline void* memmove(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    
    if (d < s) {
        // Copy forward
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        // Copy backward
        for (uint64_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

static inline int strncmp(const char *s1, const char *s2, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (uint8_t)s1[i] - (uint8_t)s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}
