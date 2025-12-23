.section .text
.globl mtrap_entry
.option norvc
.align 2

#define CLINT_BASE 0x02000000
#define MTIMECMP_OFF 0x4000
#define MTIME_OFF 0xBFF8
#define MIP_SSIP (1 << 1)
#define INTERVAL 10000000

mtrap_entry:
    
    addi sp, sp, -32
    sd t0, 0(sp)
    sd t1, 8(sp)
    sd t2, 16(sp)
    sd t3, 24(sp)

    csrr t0, mcause
    srli t1, t0, 63
    beqz t1, spin

    /*mcause & 0xfff*/
    li t2, 0xfff 
    and t2, t0, t2
    li t3, 7 
    bne t2, t3, spin

    csrr t0, mhartid

    /*mtimecmp = clintbase + mtimecmpoff + hartid8*/
    li t1, CLINT_BASE + MTIMECMP_OFF
    slli t2, t0, 3
    add t1, t1, t2

    /*now = *(mtime)*/
    li t2, CLINT_BASE + MTIME_OFF
    ld t3, 0(t2)

    /**(mtimecmp) = now + int*/
    li t0, INTERVAL
    add t3, t3, t0
    sd t3, 0(t1)

    li t0, MIP_SSIP
    csrs mip, t0

    ld t0, 0(sp)
    ld t1, 8(sp)
    ld t2, 16(sp)
    ld t3, 24(sp)
    addi sp, sp, 32
    mret 

spin:
    j spin