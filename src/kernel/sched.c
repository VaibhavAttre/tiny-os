#include "kernel/sched.h"
#include "kernel/printf.h"
#include "timer.h"


/*
https://danielmangum.com/posts/risc-v-bytes-timer-interrupts/
https://book.rvemu.app/hardware-components/03-csrs.html
*/

#define QUANT_TICKS 20

volatile int need_switch = 0;

void yield() {
    need_switch = 0;
    //kprintf("no cntxt switch\n");
}

void sched_ticks() {
    if((ticks % QUANT_TICKS) == 0) {
        need_switch = 1;
    }
}