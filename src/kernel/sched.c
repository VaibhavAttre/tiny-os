#include "kernel/sched.h"
#include "kernel/printf.h"

volatile int need_switch = 0;

void yield() {
    need_switch = 0;
    kprintf("no cntxt switch\n");
}