#include <stdint.h>
#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>
#include "kernel/sched.h"
#include "kernel/kalloc.h"
#include "kernel/vm.h"
#include "riscv.h"

void kmain(void) {
    uart_init();
    trap_init();

    kinit();
    kvminit();
    kvmenable();

    set_csr_bits(sie, SIE_SSIE);
    sstatus_enable_sie();

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

    //BOOTED CORRECLTY SO FAR
    kprintf("tiny-os booted\n");

    extern char __rodata_start[];
    kprintf("testing rodata write at %p (should fault)\n", __rodata_start);
    *(volatile char*)__rodata_start = 1;  // should fault if rodata is mapped R-only

    for (;;) {
        asm volatile("wfi");
        if (need_switch) yield();
    }
}
