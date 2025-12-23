#include <stdint.h>
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/sched.h"

#define TICK_HZ 10

void clockinterrupt() {

    ticks++;
    sched_tick();
}