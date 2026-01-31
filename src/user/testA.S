.section .text
.globl _start
.option norvc
.align 2

.equ SYS_PUTC, 1
.equ SYS_EXEC, 7
.equ SYS_EXIT, 6
.equ SYS_WRITE, 9

_start:
    # init gp for small-data references (prevent relaxation)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # Test the new write() syscall with FD 1 (stdout)
    # write(fd=1, buf=msg, len=18)
    li a0, 1            # fd = stdout
    la a1, msg          # buf = address of message
    li a2, 18           # len = 18 bytes
    li a7, SYS_WRITE
    ecall
    # a0 now contains bytes written (should be 18)

    # Exec into testB
    li a0, 1
    li a7, SYS_EXEC
    ecall

    # If exec returns, print error using old putc
    li a0, '!'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li a0, 0
    li a7, SYS_EXIT
    ecall

.section .rodata
msg:
    .ascii "Hello from FD 1!\n\0"
