.section .text
.globl _start
.option norvc
.align 2

.equ SYS_PUTC, 1
.equ SYS_EXEC, 7
.equ SYS_EXIT, 6

_start:
    li a0, 'A'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li a0, 1
    li a7, SYS_EXEC
    ecall

    li a0, '!'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li a0, 0
    li a7, SYS_EXIT
    ecall
