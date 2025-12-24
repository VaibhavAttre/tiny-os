#pragma once
#include <stdint.h>


#define NPROC 512
#define KSTACKS 1
#define KSTACK_SIZE (4096 * KSTACKS)
#define QUANT_TICKS 5
#define HZ 100

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
};

void sched_init();
int sched_create_kthread(void (*func)(void));
void scheduler();
void yield();
void sleep(void * chan);
void wakeup(void * chan);
void sched_tick();
void sched_on_tick();
void sched_dump();
void sleep_ticks(uint64_t t);
void sleep_ms(uint64_t ms);
void sleep_until(uint64_t t);

//implementedin assmebly 
void swtch(struct context * old, struct context * new);
