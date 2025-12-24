.section .text
.globl _start
.align 2

_start:

    la   t0, mtrap
    andi t0, t0, -4
    csrw mtvec, t0

    csrr t0, mstatus
    li   t1, ~(1 << 3)       
    and  t0, t0, t1
    csrw mstatus, t0

    li   t0, 0
    csrw mie, t0

    la   sp, __stack_top
    andi sp, sp, -16

    la   t0, __bss_start
    la   t1, __bss_end
1:
    bge  t0, t1, 2f
    sb   zero, 0(t0)
    addi t0, t0, 1
    j    1b

2:
    li   t0, -1             
    csrw pmpaddr0, t0
    li   t0, 0x0f           
    csrw pmpcfg0, t0

    la   t0, trap_entry
    andi t0, t0, -4
    csrw stvec, t0

    li   t0, (1<<1) //SSIP bit
    //csrw medeleg, t0
    csrw mideleg, t0

    csrr t0, mstatus
    li   t1, ~(3 << 11)     
    and  t0, t0, t1
    li   t1, (1 << 11)       
    or   t0, t0, t1
    ori  t0, t0, (1 << 7)    
    csrw mstatus, t0

    la   t0, kmain
    csrw mepc, t0

    li   t0, 0
    csrw satp, t0

    la t0, mtrap_entry
    csrw mtvec, t0

    li t0, (1 << 7)
    csrs mie, t0 /*enable machine timer interrupts*/

    li  t0, (1<<2) | (1<<3) | (1<<8) | (1<<9) | (1<<12) | (1<<13) | (1<<15)
    csrw medeleg, t0


    li t0, (1 <<1) /*delegate software interrupts to smode*/
    csrw mideleg, t0

    csrr t1, mhartid
    li t0, 0x02004000 //clintbase + 4000
    slli t2, t1, 3 
    add t0, t0, t2 // &mtimecmp[hart]

    li t3, 0x0200BFF8 //&mtime
    ld t4, 0(t3) //now
    li t5, 100000//10000000 //interval
    add t4, t4, t5
    sd t4, 0(t0)
    li t0, 0x7
    csrw mcounteren, t0

    mret

    j mtrap                

.size _start, .-_start

.align 2
.globl mtrap
mtrap:
    li t0, 0x10000000
    li t1, 'M'
    sb t1, 0(t0)
1:  j 1b

.extern __bss_start
.extern __bss_end
.extern __stack_top
.extern kmain
.extern trap_entry