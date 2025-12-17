#include <stdint.h>
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/sched.h"

#define TICK_HZ 100
#define QUANT_TICKS 20

void clockinterrupt() {

    ticks++;
    sched_tick();
}