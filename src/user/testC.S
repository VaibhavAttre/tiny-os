.section .text
.globl _start
.option norvc
.align 2

.equ SYS_EXIT, 6
.equ SYS_WRITE, 9
.equ SYS_OPEN, 11
.equ SYS_READ, 8
.equ SYS_CLOSE, 10
.equ SYS_TRUNCATE, 21

.equ O_RDONLY, 0x000
.equ O_WRONLY, 0x001
.equ O_CREATE, 0x200
.equ O_TREE,   0x800

_start:
    # init gp for small-data references (prevent relaxation)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # Open a file for writing (create it)
    la a0, filename
    li a1, (O_WRONLY | O_CREATE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0

    # Write data
    mv a0, s0
    la a1, content
    li a2, 8
    li a7, SYS_WRITE
    ecall
    bltz a0, fail

    # Close
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # Truncate to 0
    la a0, filename
    li a1, 0
    li a7, SYS_TRUNCATE
    ecall
    bltz a0, fail

    # Reopen for reading
    la a0, filename
    li a1, O_RDONLY
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0

    # Read: expect 0 bytes
    mv a0, s0
    la a1, read_buf
    li a2, 16
    li a7, SYS_READ
    ecall
    bnez a0, fail

    # Close
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # PASS
    li a0, 1
    la a1, msg_pass
    li a2, 12
    li a7, SYS_WRITE
    ecall
    j done

fail:
    li a0, 1
    la a1, msg_fail
    li a2, 12
    li a7, SYS_WRITE
    ecall

done:
    li a0, 0
    li a7, SYS_EXIT
    ecall

.section .rodata
filename:
    .asciz "/trunc.txt"
content:
    .ascii "truncate"
msg_pass:
    .ascii "testC: PASS\n"
msg_fail:
    .ascii "testC: FAIL\n"

.section .bss
.align 3
read_buf:
    .space 16
