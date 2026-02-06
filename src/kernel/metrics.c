#include "kernel/metrics.h"
#include "kernel/string.h"
#include "riscv.h"
#include "timer.h"

struct tiny_metrics global_metrics;

void metrics_init() {
    memzero(&global_metrics, sizeof(global_metrics));
    global_metrics.version = TINY_METRICS_VERSION;
}

void metrics_snapshot(struct tiny_metrics * out) {
    
    if (!out) {
        return;
    }

    int wason = (r_sstatus() & SSTATUS_SIE) != 0;
    if (wason) {
        sstatus_disable_sie();
    }

    global_metrics.ticks = ticks;
    memcopy(out, &global_metrics, sizeof(global_metrics));

    if(wason) sstatus_enable_sie();
}