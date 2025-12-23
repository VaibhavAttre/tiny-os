#include <stdint.h>
#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>
#include "kernel/sched.h"
#include "kernel/kalloc.h"
#include "kernel/vm.h"
#include "riscv.h"

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
extern volatile uint64_t ticks;  

static void threadA(void) {
    uint64_t last = 0;
    for (;;) {
        if (ticks != last) {      // once per tick
            last = ticks;
            kprintf("[A] tick=%d\n", (int)ticks);
        }
    }
}

static int bchan;
static void *CHAN_B = &bchan;

static void threadB(void) {
    uint64_t last = 0;
    uint64_t seen = 0;

    for (;;) {
        if (ticks != last) {
            last = ticks;
            seen++;

            kprintf("  [B] tick=%d\n", (int)ticks);

            if (seen % 6 == 0) {
                kprintf("  [B] sleeping on CHAN_B...\n");
                sleep(CHAN_B);
                kprintf("  [B] woke!\n");
            }
        }
    }
}

static void threadC(void) {
    uint64_t last = 0;
    uint64_t seen = 0;

    for (;;) {
        if (ticks != last) {
            last = ticks;
            seen++;

            kprintf("    [C] tick=%d\n", (int)ticks);

            if (seen % 6 == 2) {
                kprintf("    [C] wakeup(CHAN_B)\n");
                wakeup(CHAN_B);
            }
        }
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

    if (sched_create_kthread(threadA) < 0) { kprintf("failed to create threadA\n"); while(1){} }
    if (sched_create_kthread(threadB) < 0) { kprintf("failed to create threadB\n"); while(1){} }
    if (sched_create_kthread(threadC) < 0) { kprintf("failed to create threadC\n"); while(1){} }

    scheduler();

    for (;;) {
        asm volatile("wfi");
    }
}
