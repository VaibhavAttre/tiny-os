#ifndef DRIVERS_UART_H
#define DRIVERS_UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int uart_getc(void);  // Returns -1 if no data available

#endif