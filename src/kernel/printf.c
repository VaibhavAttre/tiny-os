//printf.c
#include <kernel/printf.h>

void consputchar(char c) {
    if(c == '\n') {
        uart_putc('\r');
    }   
    uart_putc(c);
}

static void printptr(uint64_t ptr) {

    consputchar('0');
    consputchar('x');
    for(int i = 60; i >= 0; i -= 4) {
        consputchar("0123456789abcdef"[(ptr >> i) & 0xF]);
    }
}

static void printnum(long long x, int base, int sign) {

    char buf[32];
    int i;
    unsigned long long ux;

    if (sign && (sign = x < 0)) {
        ux = -x;
    } else {
        ux = x;
        sign = 0;
    }

    i = 0;
    do {
        buf[i++] = "0123456789abcdef"[ux % base];
        ux /= base;
    } while (ux);
    
    if (sign) {
        buf[i++] = '-';
    }
    while (--i >= 0) {
        consputchar(buf[i]);
    }
}

int kprintf(const char *fmt, ...) {

    va_list ap;
    const char *p;
    int i;
    uint64_t v;
    char *s;

    va_start(ap, fmt);
    for (p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            consputchar(*p);
            continue;
        }
        p++;    
        switch (*p) {
            case 'd':
                i = va_arg(ap, int);
                printnum(i, 10, 1);
                break;
            case 'x':
                i = va_arg(ap, int);
                printnum(i, 16, 0);
                break;
            
            case 'u':
                i = va_arg(ap, int);
                printnum(i, 10, 0);
                break;

            case 's':
                s = va_arg(ap, char *);
                for (; *s; s++) {
                    consputchar(*s);
                }
                break;
            case 'c':
                i = va_arg(ap, int);
                consputchar((char)i);
                break;
            case '%':
                consputchar('%');
                break;
            case 'p':
                v = va_arg(ap, uint64_t);
                printptr(v);
                break;
            default:
                consputchar('%');
                consputchar(*p);
                break;
        }
    }
    va_end(ap);
    return 0;
}