    .section .text
    .globl _start

    .equ SYS_PUTC,   1
    .equ SYS_YIELD,  2
    .equ SYS_TICKS,  3
    .equ SYS_SLEEP,  4
    .equ SYS_GETPID, 5
    .equ SYS_EXIT,   6

_start:
    # pid = getpid()
    li a7, SYS_GETPID
    ecall
    mv s0, a0          # s0 = pid

    # print pid digit + '\n'
    addi a0, s0, '0'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li t0, 1
    beq s0, t0, do_fault

# pid 0: print P for ~50 ticks then exit(0)
do_run_then_exit:
    li a7, SYS_TICKS
    ecall
    mv s1, a0          # start ticks

loop_p:
    li a0, 'P'
    li a7, SYS_PUTC
    ecall   

    # be nice: yield so output interleaves clearly
    li a7, SYS_YIELD
    ecall

    li a7, SYS_TICKS
    ecall
    sub t1, a0, s1
    li t2, 50
    blt t1, t2, loop_p

    # print 'E\n'
    li a0, 'E'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    # exit(0)
    li a0, 0
    li a7, SYS_EXIT
    ecall

hang0:
    j hang0

# pid 1: print 'F\n' then cause a store page fault (unmapped VA)
do_fault:
    li a0, 'F'
    li a7, SYS_PUTC
    ecall
    li a0, '\n'
    li a7, SYS_PUTC
    ecall

    li t3, 0x8000      # should be unmapped in your tiny user VA layout
    sd zero, 0(t3)     # store -> exception code 15 (store page fault)

hang1:
    j hang1
