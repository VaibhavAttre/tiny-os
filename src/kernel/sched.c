#include "kernel/sched.h"
#include "kernel/kalloc.h"
#include "kernel/printf.h"
#include "riscv.h"
#include "timer.h"


/*
https://danielmangum.com/posts/risc-v-bytes-timer-interrupts/
https://book.rvemu.app/hardware-components/03-csrs.html
*/


volatile int need_switch = 0;
volatile int in_scheduler = 0;
static struct proc procs[NPROC];
static struct context scheduler_context;
static struct proc * curr = 0;

//bootstrap for first time proc is run
static void kthread_trampoline() {

    void (*func)(void) = curr->start;
    func();

    kprintf("kthread trampoline\n");
    curr->state = UNUSED;
    yield();

    for(;;) {
        asm volatile("wfi");
    }
}

void sched_init() {

    for(int i = 0; i < NPROC; ++i) {

        procs[i].state = UNUSED;
        procs[i].start = 0;
        procs[i].kstack_base = 0;
        procs[i].kstack_top = 0;

        procs[i].ctx.ra = 0;
        procs[i].ctx.sp = 0;
        procs[i].ctx.s0 = 0;
        procs[i].ctx.s1 = 0;
        procs[i].ctx.s2 = 0;
        procs[i].ctx.s3 = 0;
        procs[i].ctx.s4 = 0;
        procs[i].ctx.s5 = 0;
        procs[i].ctx.s6 = 0;
        procs[i].ctx.s7 = 0;
        procs[i].ctx.s8 = 0;
        procs[i].ctx.s9 = 0;
        procs[i].ctx.s10 = 0;
        procs[i].ctx.s11 = 0;
    }
}

int sched_create_kthread(void (*func)(void)) {

    for(int i = 0; i < NPROC; ++i) {

        if (procs[i].state == UNUSED) {

            void * stack_base = kalloc();
            if(!stack_base) {
                panic("sched create failed\n");
                return -1;
            }            

            procs[i].kstack_base = stack_base;
            procs[i].kstack_top = (uint64_t)stack_base + KSTACK_SIZE;
            procs[i].start = func;

            procs[i].ctx.sp = procs[i].kstack_top;
            procs[i].ctx.ra = (uint64_t)kthread_trampoline;
            procs[i].state = RUNNABLE;
            return 0;
        }
    }
    return -1;
}

void yield() {
    
    if(!curr) {
        need_switch = 0;
        return;
    }

    need_switch = 0;
    curr->state = RUNNABLE;
    swtch(&curr->ctx, &scheduler_context);
}

//Round robin for now
void scheduler() {

    for(;;) {
        int ran = 0;
        in_scheduler = 1;
        for(int i = 0; i < NPROC; ++i) {

            if (procs[i].state != RUNNABLE) continue;
            ran = 1;
            curr = &procs[i];
            curr->state = RUNNING;
            in_scheduler = 0;
            sstatus_enable_sie();
            swtch(&scheduler_context, &curr->ctx);
            in_scheduler = 1;
            //after it yeilds 
            curr = 0;
        }

        if(!ran) {
            sstatus_enable_sie();
            asm volatile("wfi");
        }
    }
}

void sched_tick() {

    if((ticks% QUANT_TICKS) != 0) return;

    need_switch = 1;
}