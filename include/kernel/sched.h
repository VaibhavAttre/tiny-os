#pragma once
#include <stdint.h>


#define NPROC 3
#define KSTACKS 1
#define KSTACK_SIZE (4096 * KSTACKS)
#define QUANT_TICKS 2

extern volatile int need_switch;
extern volatile int in_scheduler;

typedef enum {
    UNUSED = 0,
    RUNNABLE = 1,
    RUNNING = 2,
    SLEEPING = 3,
} proc_state_t;

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
    proc_state_t state;
    struct context ctx;
    void (*start)(void);
    void * kstack_base;
    uint64_t kstack_top;
    void * chan;
};

void sched_init();
int sched_create_kthread(void (*func)(void));
void scheduler();
void yield();
void sleep(void * chan);
void wakeup(void * chan);
void sched_tick();

//implementedin assmebly 
void swtch(struct context * old, struct context * new);
