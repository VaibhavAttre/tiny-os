#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>
#include "kernel/sched.h"
#include "riscv.h"
#include "kernel/vm.h"

void kmain(void) {

    asm volatile("csrr t0, sstatus\n"
                 "ori t0, t0, 0x2\n"  
                 "csrw sstatus, t0");
                 
    //init systems
    uart_init();
    trap_init();    
    kinit();
    kvminit();
    kvmenable();
    set_csr_bits(sie, SIE_SSIE);
    sstatus_enable_sie();

    kprintf("tiny-os booted\n");

    kprintf("about to fault\n");
    volatile uint64_t x = *(volatile uint64_t*)0x0;
    (void)x;
    kprintf("you should NOT see this\n");


    while (1)
    {
        asm volatile("wfi");
        if(need_switch) {
            yield();
        }
    }
    
}