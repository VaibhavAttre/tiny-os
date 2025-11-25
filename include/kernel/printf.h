#ifndef PRINTF_H
#define PRINTF_H

#include <stdint.h>
#include <stdarg.h>
#include <drivers/uart.h>

void consputchar(char c);

int kprintf(const char *fmt, ...);
void panic(const char *msg) __attribute__((noreturn));

#endif