#include <drivers/uart.h>
#include <stdint.h>

#define UART0 ((volatile uint8_t *)0x10000000)

void uart_init() {

}

void uart_putc(char c) {
    *UART0 = (uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

