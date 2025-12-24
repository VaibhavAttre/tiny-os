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
    sstatus_enable_sie();
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
        procs[i].chan = 0;
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

        procs[i].st = (struct sched_stats){0};
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
            procs[i].chan = 0;
            procs[i].id = i;
            *(struct proc **) stack_base = &procs[i];

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

    int was_on = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_disable_sie();

    if(need_switch) curr->st.involuntary_yields++;
    else curr->st.voluntary_yields++;

    need_switch = 0;
    curr->state = RUNNABLE;
    swtch(&curr->ctx, &scheduler_context);

    if(was_on) sstatus_enable_sie();
}

void sleep(void * chan) {

    if(!curr) return;

    int wasinterrupton = (r_sstatus() & SSTATUS_SIE) != 0;

    sstatus_disable_sie();

    curr->st.sleep_calls++;
    curr->st.sleep_start_tick = ticks;

    curr->chan = chan;
    curr->state = SLEEPING;
    need_switch = 0;
    swtch(&curr->ctx, &scheduler_context);
    curr->chan = 0;
    if(curr->st.sleep_start_tick) {
        curr->st.slept_ticks_total += (ticks - curr->st.sleep_start_tick);
        curr->st.sleep_start_tick = 0;
    }
    if(wasinterrupton) sstatus_enable_sie();
}

void wakeup(void * chan) {

    int wasinterrupton = (r_sstatus() & SSTATUS_SIE) != 0;

    sstatus_disable_sie();
    for(int i = 0; i < NPROC; ++i) {

        if(procs[i].state == SLEEPING && procs[i].chan == chan) {
            procs[i].state = RUNNABLE;
            procs[i].chan = 0;

            procs[i].st.wakeups++;
            procs[i].st.last_wakeup_tick = ticks;
        }
    }

    if(wasinterrupton) sstatus_enable_sie();
}

//Round robin for now
void scheduler() {

    for(;;) {
        int ran = 0;
        in_scheduler = 1;
        sstatus_disable_sie();
        for(int i = 0; i < NPROC; ++i) {

            if (procs[i].state != RUNNABLE) continue;
            ran = 1;
            curr = &procs[i];
            curr->state = RUNNING;
            
            curr->st.ctx_in++;
            if(curr->st.last_wakeup_tick) {
                curr->st.wake_latency_total += (ticks - curr->st.last_wakeup_tick);
                curr->st.wake_latency_events++;
                curr->st.last_wakeup_tick = 0;
            }

            in_scheduler = 0;
            sstatus_enable_sie();
            swtch(&scheduler_context, &curr->ctx);
            sstatus_disable_sie();
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

void sched_on_tick() {

    if(curr && curr->state == RUNNING) {
        curr->st.run_ticks++;
    }
}

void sched_tick() {

    if((ticks% QUANT_TICKS) != 0) return;

    need_switch = 1;
}

void sched_dump() {

    kprintf("CSV\n");
    kprintf("id,run_ticks,ctx_in,preemptions,voluntary_yields,sleep_calls,wakeups_received,slept_ticks_total,avg_wake_latency_ticks\n");
    for (int i=0; i<NPROC; i++) {
        if (procs[i].kstack_base == 0) continue; // allocated thread
        uint64_t avg = 0;
        if (procs[i].st.wake_latency_events)
            avg = procs[i].st.wake_latency_total / procs[i].st.wake_latency_events;

        kprintf("%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            procs[i].id,
            (int)procs[i].st.run_ticks,
            (int)procs[i].st.ctx_in,
            (int)procs[i].st.involuntary_yields,
            (int)procs[i].st.voluntary_yields,
            (int)procs[i].st.sleep_calls,
            (int)procs[i].st.wakeups,
            (int)procs[i].st.slept_ticks_total,
            (int)avg
        );
    }
    kprintf("CSV_END\n");
}

void sleep_ticks(uint64_t t) {

    if(t == 0) return;
    uint64_t target = ticks + t;
    while((int64_t)(ticks - target) < 0) {
        sleep((void*)&ticks);
    }
}

void sleep_until(uint64_t t) {
    while ((int64_t)(ticks - t) < 0) {
        sleep((void*)&ticks);
    }
}

void sleep_ms(uint64_t ms) {

    uint64_t t = (ms * HZ+999)/1000;
    if(t == 0) t = 1;
    sleep_ticks(t);
}