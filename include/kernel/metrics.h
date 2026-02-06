#pragma once
#include <stdint.h>

#define TINY_METRICS_VERSION 1

struct tiny_metrics {

    uint64_t version;
    uint64_t ticks; 
    
    uint64_t syscall_enter;
    uint64_t syscall_exit;

    uint64_t context_switches;

    uint64_t page_faults;

    uint64_t disk_reads;
    uint64_t disk_writes;
    uint64_t disk_read_bytes;
    uint64_t disk_write_bytes;
};

extern struct tiny_metrics global_metrics;

void metrics_init();
void metrics_snapshot(struct tiny_metrics * out);

static inline void metrics_inc_u64(uint64_t *p, uint64_t v) {
    __sync_fetch_and_add(p, v);
}