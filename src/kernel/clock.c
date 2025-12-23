#include <stdint.h>
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/sched.h"

void clockinterrupt() {

    ticks++;
    sched_tick();
}