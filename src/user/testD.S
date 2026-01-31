.section .text
.globl _start
.option norvc
.align 2

.equ SYS_EXIT, 6
.equ SYS_WRITE, 9
.equ SYS_OPEN, 11
.equ SYS_READ, 8
.equ SYS_CLOSE, 10

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
    # Print start message
    li a0, 1
    la a1, msg_start
    li a2, 12
    li a7, SYS_WRITE
    ecall

    # Open a tree-backed file for writing (create it)
    la a0, filename
    li a1, (O_WRONLY | O_CREATE | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0

    # Write data
    mv a0, s0
    la a1, content
    li a2, 10
    li a7, SYS_WRITE
    ecall
    bltz a0, fail

    # Print after write
    li a0, 1
    la a1, msg_write
    li a2, 13
    li a7, SYS_WRITE
    ecall

    # Close
    mv a0, s0
    li a7, SYS_CLOSE
    ecall

    # Reopen for reading
    la a0, filename
    li a1, (O_RDONLY | O_TREE)
    li a7, SYS_OPEN
    ecall
    bltz a0, fail
    mv s0, a0

    # Read back
    mv a0, s0
    la a1, read_buf
    li a2, 10
    li a7, SYS_READ
    ecall
    bltz a0, fail

    # Print after read
    li a0, 1
    la a1, msg_read
    li a2, 12
    li a7, SYS_WRITE
    ecall

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
    .asciz "/tree.txt"
content:
    .ascii "tree data!"
msg_pass:
    .ascii "testD: PASS\n"
msg_fail:
    .ascii "testD: FAIL\n"
msg_start:
    .ascii "testD: start\n"
msg_write:
    .ascii "testD: wrote\n"
msg_read:
    .ascii "testD: read\n"

.section .bss
.align 3
read_buf:
    .space 16
