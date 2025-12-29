.section .text
.globl _start
.option norvc
.align 2

.equ SYS_PUTC, 1
.equ SYS_EXIT, 6

_start:
    li a0, 'B'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li a0, 0
    li a7, SYS_EXIT
    ecall
    