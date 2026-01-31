.section .text
.globl _start
.option norvc
.align 2

.equ SYS_EXIT, 6

_start:
    # init gp for small-data references (prevent relaxation)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    call main

    # exit(main_ret)
    mv a0, a0
    li a7, SYS_EXIT
    ecall

1:
    j 1b
