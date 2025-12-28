#include <stdint.h>
#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>
#include "kernel/sched.h"
#include "kernel/kalloc.h"
#include "kernel/vm.h"
#include "riscv.h"
#include "kernel/syscall.h"
#include "user_test.h"

extern volatile uint64_t ticks;  
#define RUN_FOR_TICKS 50000
static volatile uint64_t sink = 0; 
#define DUMP_EVERY 200

extern unsigned char user_test_bin[];
extern unsigned int user_test_bin_len;

static inline void do_ecall_putc(char c) {
    register uint64_t a0 asm("a0") = (uint64_t)c;
    register uint64_t a7 asm("a7") = SYSCALL_PUTC;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void thread_ecall_test(void) {
    for (;;) {
        do_ecall_putc('S');
        sleep_ticks(50);
    }
}


void test_vm() {
    
    /*Simple VM tests*/
    uint64_t satp = read_csr(satp);
    kprintf("satp=%p mode=%d\n", (void*)satp, (int)(satp >> 60));
    if ((satp >> 60) != 8) { kprintf("ERROR: not Sv39\n"); while(1){} }

    void *pg = kalloc();
    if(!pg){ kprintf("kalloc failed\n"); while(1){} }

    volatile uint64_t *p = (volatile uint64_t*)pg;
    p[0] = 0x1122334455667788ULL;
    p[1] = 0xA5A5A5A5A5A5A5A5ULL;

    if(p[0] != 0x1122334455667788ULL || p[1] != 0xA5A5A5A5A5A5A5A5ULL){
        kprintf("kalloc page readback mismatch\n");
        while(1){}
    }
    kfree(pg);
    kprintf("heap page RW ok\n");

}

static inline uint64_t rdcycle64() {
    uint64_t x;
    asm volatile("rdcycle %0" : "=r"(x));
    return x;
}

//found online: xorshift64
static inline uint64_t rng_next(uint64_t *seed) {

    uint64_t x = *seed;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *seed = x;
    return x * 2685821657736338717ULL;
}

static inline uint64_t rng_range(uint64_t* seed, uint64_t a, uint64_t b) {

    uint64_t diff = b - a + 1;
    return a + (rng_next(seed) % diff);
}

static inline void busy_cycles(uint64_t cycles) {

    uint64_t start = rdcycle64();
    while((rdcycle64() - start) < cycles) {
        sink ^= start + 0x9e3779b97f4a7c15ULL;
        sink += (sink << 7) ^ (sink >> 3);
    }
}

static void run_for_ticks(uint64_t *s, uint64_t t) {
    uint64_t end = ticks + t;
    while ((int64_t)(ticks - end) < 0) {
        busy_cycles(rng_range(s, 20000, 200000));
        if ((rng_next(s) & 7) == 0) yield();
    }
}

// worker threads

static void thread_batch0(void) {
    uint64_t s = 0xBEEF0000ULL ^ rdcycle64();
    for (;;) {
        busy_cycles(rng_range(&s, 80000, 600000));
        if ((rng_next(&s) & 3ULL) == 0) yield(); // 25% chance
    }
}

static void thread_batch1(void) {
    uint64_t s = 0xBEEF1111ULL ^ (rdcycle64() << 1);
    for (;;) {
        busy_cycles(rng_range(&s, 80000, 600000));
        if ((rng_next(&s) & 7ULL) == 0) yield(); // 12.5% chance
    }
}

static void thread_interactive0(void) {
    uint64_t s = 0x1A0D0ULL ^ rdcycle64();
    for (;;) {
        run_for_ticks(&s, rng_range(&s, 1, 8));
        //sleep_ticks(rng_range(&s, 5, 60));
    }
}

static void thread_interactive1(void) {
    uint64_t s = 0x1A0D1ULL ^ (rdcycle64() + 123);
    for (;;) {
        run_for_ticks(&s, rng_range(&s, 1, 8));
        //sleep_ticks(rng_range(&s, 5, 60));
    }
}

static void thread_interactive2(void) {
    uint64_t s = 0x1A0D2ULL ^ (rdcycle64() + 456);
    for (;;) {
        run_for_ticks(&s, rng_range(&s, 1, 8));
        //sleep_ticks(rng_range(&s, 5, 60));
    }
}

static void thread_interactive3(void) {
    uint64_t s = 0x1A0D3ULL ^ (rdcycle64() + 789);
    for (;;) {
        run_for_ticks(&s, rng_range(&s, 1, 8));
        //sleep_ticks(rng_range(&s, 5, 60));
    }
}

static void thread_io(void) {
    uint64_t s = 0x1010ULL ^ (rdcycle64() << 2);
    for (;;) {
        run_for_ticks(&s, rng_range(&s, 1, 2));
        //sleep_ticks(rng_range(&s, 20, 200));
    }
}

// 1× stats: periodic snapshots + final dump
static void thread_stats(void) {
    uint64_t next = DUMP_EVERY;
    

    while (ticks < RUN_FOR_TICKS) {
        
        if (ticks < next) {
            sleep_ticks(next - ticks);
        }

        
        kprintf("SNAPSHOT,tick=%d\n", (int)ticks);
        sched_dump();

        next += DUMP_EVERY;
    }

    kprintf("FINAL,tick=%d\n", (int)ticks);
    sched_dump();
    for (;;) asm volatile("wfi");
}

static void stress_test_threads() {

    if (sched_create_kthread(thread_batch0) < 0) { kprintf("failed batch0\n"); while(1){} }
    if (sched_create_kthread(thread_batch1) < 0) { kprintf("failed batch1\n"); while(1){} }
    
    if (sched_create_kthread(thread_interactive0) < 0) { kprintf("failed int0\n"); while(1){} }
    if (sched_create_kthread(thread_interactive1) < 0) { kprintf("failed int1\n"); while(1){} }
    if (sched_create_kthread(thread_interactive2) < 0) { kprintf("failed int2\n"); while(1){} }
    if (sched_create_kthread(thread_interactive3) < 0) { kprintf("failed int3\n"); while(1){} }
    
    if (sched_create_kthread(thread_io) < 0) { kprintf("failed ioish\n"); while(1){} }
    //kprintf("Reached");
    if (sched_create_kthread(thread_stats) < 0) { kprintf("failed stats\n"); while(1){} }
    //kprintf("Reached");
    
}

static void thread_trace_printer(void) {
    for (;;) {
        sleep_ticks(50);

        kprintf("\n--- TRACE @ tick=%d ---\n", (int)ticks);
        uint32_t r, w;
        sched_trace_state(&r, &w);
        kprintf("TRACE state: r=%u w=%u\n", (unsigned)r, (unsigned)w);

        int total = 0;
        while (total < 200) {
            int printed = sched_trace_dump_n(40);
            if (printed == 0) break;
            total += printed;
        }
    }
}


static void thread_kalloc_stress(void) {
    uint64_t iter = 0;
    for (;;) {
        void *p = kalloc();
        if (p) {
            ((volatile uint64_t*)p)[0] = ticks;
            ((volatile uint64_t*)p)[1] = 0xdeadbeef;
            kfree(p);
        }

        iter++;
        if ((iter & 7) == 0) yield();  
        else sleep_ticks(1);
    }
}


static void thread_kernel_yielder(void) {
    for (;;) {
        for (volatile int i=0; i<5000; i++) {}
        yield();
    }
}



void kmain(void) {
    uart_init();
    trap_init();

    kinit();
    kvminit();
    kvmenable();
    sched_init();

    set_csr_bits(sie, SIE_SSIE);
    sstatus_enable_sie();

    //test_vm();
   
    //BOOTED CORRECLTY SO FAR
    kprintf("tiny-os booted\n");

    //sched_create_kthread(thread_ecall_test);
    
    //sched_create_kthread(thread_trace_printer);
    //sched_create_kthread(thread_kalloc_stress);
    //sched_create_kthread(thread_kernel_yielder);

    /*for(int i = 0; i < 6; ++i) {
        if (sched_create_userproc(user_test_bin, (uint64_t)user_test_bin_len) < 0) {
            kprintf("failed to create userproc %d\n", i);
            while(1){}
        }
    }*/

    sched_create_userproc(user_test_bin, (uint64_t)user_test_bin_len);
    sched_create_userproc(user_test_bin, user_test_bin_len);
    //sched_create_userproc(user_test_bin, user_test_bin_len);
    scheduler();

    for (;;) {
        asm volatile("wfi");
    }
}
