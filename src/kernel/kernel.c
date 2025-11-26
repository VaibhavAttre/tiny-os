#include <drivers/uart.h>
#include <kernel/printf.h>
#include <kernel/trap.h>

void kmain(void) {

    asm volatile("csrr t0, sstatus\n"
                 "ori t0, t0, 0x2\n"  
                 "csrw sstatus, t0");
                 
    //init systems
    uart_init();
    kprintf("HII\n");
    trap_init();    
    
    //panic("Kernel encountered a fatal error.");
    kprintf("%d\n", 1+2);
    asm volatile ("ecall");
    kprintf("Back to kmain after ecall\n");

    kprintf("Testing illegal instr\n");
    asm volatile (".word 0xffffffff"); //illegal instruction
    kprintf("Back to kmain after illegal instr\n");

    while (1)
    {
        /* code */
    }
    
}