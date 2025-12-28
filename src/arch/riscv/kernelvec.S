// src/arch/riscv/kernelvec.S
.section .text
.globl kernelvec
.align 2
.option norvc

#define TF_RA      0
#define TF_SP      8
#define TF_GP      16
#define TF_TP      24
#define TF_T0      32
#define TF_T1      40
#define TF_T2      48
#define TF_S0      56
#define TF_S1      64
#define TF_A0      72
#define TF_A1      80
#define TF_A2      88
#define TF_A3      96
#define TF_A4      104
#define TF_A5      112
#define TF_A6      120
#define TF_A7      128
#define TF_S2      136
#define TF_S3      144
#define TF_S4      152
#define TF_S5      160
#define TF_S6      168
#define TF_S7      176
#define TF_S8      184
#define TF_S9      192
#define TF_S10     200
#define TF_S11     208
#define TF_T3      216
#define TF_T4      224
#define TF_T5      232
#define TF_T6      240
#define TF_SEPC    248
#define TF_SSTATUS 256
#define TF_SIZE    272   

kernelvec:
    addi sp, sp, -TF_SIZE

    sd ra, TF_RA(sp)
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)

    sd t0, TF_T0(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)

    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)

    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)

    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)

    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)

    csrr t0, sepc
    sd t0, TF_SEPC(sp)
    csrr t0, sstatus
    sd t0, TF_SSTATUS(sp)

    call kerneltrap

    ld t0, TF_SEPC(sp)
    csrw sepc, t0
    ld t0, TF_SSTATUS(sp)
    csrw sstatus, t0

    ld ra, TF_RA(sp)
    ld gp, TF_GP(sp)
    ld tp, TF_TP(sp)

    ld t0, TF_T0(sp)
    ld t1, TF_T1(sp)
    ld t2, TF_T2(sp)

    ld s0, TF_S0(sp)
    ld s1, TF_S1(sp)

    ld a0, TF_A0(sp)
    ld a1, TF_A1(sp)
    ld a2, TF_A2(sp)
    ld a3, TF_A3(sp)
    ld a4, TF_A4(sp)
    ld a5, TF_A5(sp)
    ld a6, TF_A6(sp)
    ld a7, TF_A7(sp)

    ld s2, TF_S2(sp)
    ld s3, TF_S3(sp)
    ld s4, TF_S4(sp)
    ld s5, TF_S5(sp)
    ld s6, TF_S6(sp)
    ld s7, TF_S7(sp)
    ld s8, TF_S8(sp)
    ld s9, TF_S9(sp)
    ld s10, TF_S10(sp)
    ld s11, TF_S11(sp)

    ld t3, TF_T3(sp)
    ld t4, TF_T4(sp)
    ld t5, TF_T5(sp)
    ld t6, TF_T6(sp)

    addi sp, sp, TF_SIZE
    sret
