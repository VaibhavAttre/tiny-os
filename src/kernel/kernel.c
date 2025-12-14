#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>
#include "kernel/sched.h"
#include "riscv.h"

void kmain(void) {

    asm volatile("csrr t0, sstatus\n"
                 "ori t0, t0, 0x2\n"  
                 "csrw sstatus, t0");
                 
    //init systems
    uart_init();
    kprintf("tiny-os booted\n");
    trap_init();    
        
    set_csr_bits(sie, SIE_SSIE);
    sstatus_enable_sie();

    while (1)
    {
        if(need_switch) {
            yield();
        }
    }
    
}