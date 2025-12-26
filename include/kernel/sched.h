#pragma once
#include <stdint.h>
#include "sv39.h"
#include "kernel/trapframe.h"

#define NPROC 512
#define KSTACKS 1
#define KSTACK_SIZE (4096 * KSTACKS)
#define QUANT_TICKS 50
#define HZ 50

extern volatile int need_switch;
extern volatile int in_scheduler;

typedef enum {
    UNUSED = 0,
    RUNNABLE = 1,
    RUNNING = 2,
    SLEEPING = 3,
} proc_state_t;

struct sched_stats {

    uint64_t run_ticks;
    uint64_t ctx_in;
    uint64_t voluntary_yields;
    uint64_t involuntary_yields;
    uint64_t sleep_calls;
    uint64_t wakeups;
    uint64_t slept_ticks_total;
    uint64_t wake_latency_total;
    uint64_t wake_latency_events;
    uint64_t sleep_start_tick;
    uint64_t last_wakeup_tick;
};

struct context {

    uint64_t ra;
    uint64_t sp;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;    
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;   
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
};

struct proc {
    int id;
    proc_state_t state;
    struct context ctx;
    void (*start)(void);

    void * kstack_base;
    uint64_t kstack_top;
    void * chan;

    struct sched_stats st;

    pagetable_t pagetable;
    int user;
    uint64_t uentry;
    uint64_t usp;

    struct trapframe * tf;
};

void sched_init();
int sched_create_kthread(void (*func)(void));
void scheduler();
struct proc * getmyproc();
void yield();
void yield_from_trap(int preempt);
void sleep(void * chan);
void wakeup(void * chan);
void sched_tick();
void sched_on_tick();
void sched_dump();
void sched_trace_dump();
int sched_trace_dump_n(int max);
void sleep_ticks(uint64_t t);
void sleep_ms(uint64_t ms);
void sleep_until(uint64_t t);
void sched_trace_syscall(uint64_t num, uint64_t arg);
void sched_trace_state(uint32_t *r, uint32_t *w);

int sched_create_userproc(const void * code, uint64_t sz);

//implementedin assmebly 
void swtch(struct context * old, struct context * new);
