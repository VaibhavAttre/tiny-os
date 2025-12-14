#include <stdint.h>
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/sched.h"

#define Y_TICKS 200

void clockinterrupt() {

    ticks++;
    if((ticks % Y_TICKS) == 0) {
        need_switch = 1;
    }
    
}