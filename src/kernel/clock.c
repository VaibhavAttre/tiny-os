#include <stdint.h>
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/sched.h"

void clockinterrupt() {

    ticks++;
    wakeup((void*)&ticks);
    sched_on_tick();
    sched_tick();
}