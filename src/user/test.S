    .section .text
    .globl _start
    .align  2

    # syscall numbers (match your enum)
    .equ SYSCALL_PUTC,   1
    .equ SYSCALL_YIELD,  2
    .equ SYSCALL_TICKS,  3
    .equ SYSCALL_SLEEP,  4
    .equ SYSCALL_GETPID, 5

_start:
    # pid = getpid()
    li  a7, SYSCALL_GETPID
    ecall
    mv  s0, a0          # s0 = pid

loop:
    # print: [X]\n where X = 'A'+pid
    li  a7, SYSCALL_PUTC
    li  a0, '['
    ecall

    li  a7, SYSCALL_PUTC
    addi a0, s0, 65     # 'A' = 65
    ecall

    li  a7, SYSCALL_PUTC
    li  a0, ']'
    ecall

    li  a7, SYSCALL_PUTC
    li  a0, '\n'
    ecall

    # sleep(pid+2)
    li  a7, SYSCALL_SLEEP
    addi a0, s0, 2
    ecall

    # occasionally read ticks (validation)
    li  a7, SYSCALL_TICKS
    ecall

    # yield
    li  a7, SYSCALL_YIELD
    ecall

    j loop
