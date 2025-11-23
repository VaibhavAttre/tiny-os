#include <drivers/uart.h>
#include <kernel/panic.h>

void kmain(void) {

    uart_init();
    panic("Kernel encountered a fatal error.");
    while (1)
    {
        /* code */
    }
    
}