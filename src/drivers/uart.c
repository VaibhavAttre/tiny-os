//uart.c
#include <drivers/uart.h>
#include <stdint.h>

#define UART0 ((volatile uint8_t *)0x10000000)

#define UART_RHR 0
#define UART_THR 0
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_LSR 5

#define UART_LSR_TX_EMPTY (1 << 5)

static void uart_write_reg(int reg, uint8_t val) {
    
    volatile uint8_t *addr = UART0 + reg;
    *addr = val;
}

static uint8_t uart_read_reg(int reg) {
    
    volatile uint8_t *addr = UART0 + reg;
    return *addr;
}

void uart_init() {

    uart_write_reg(UART_IER, 0x00); //no interrupts 
    uart_write_reg(UART_LCR, 0x03); //0x99999911b
    uart_write_reg(UART_FCR, 0x07); //0x00000111b
}

void uart_putc(char c) {

    while ((uart_read_reg(UART_LSR) & UART_LSR_TX_EMPTY) == 0);
    uart_write_reg(UART_THR, (uint8_t)c);
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

