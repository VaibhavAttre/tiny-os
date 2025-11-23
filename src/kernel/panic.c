#include <kernel/panic.h>
#include <drivers/uart.h>

void panic(const char *msg) {
    uart_puts("PANIC: ");
    uart_puts(msg);
    uart_puts("\n");
    while (1) {
        // Halt the system
    }
}