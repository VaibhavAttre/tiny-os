#include <drivers/uart.h>
#include <kernel/printf.h>

void kmain(void) {

    uart_init();
    //panic("Kernel encountered a fatal error.");
    kprintf("%s", "Hello, Tiny OS!\n");
    while (1)
    {
        /* code */
    }
    
}