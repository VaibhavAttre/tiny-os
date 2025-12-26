.section .text
.globl trampoline
.globl uservec
.globl userret
.align 12    

.equ K_SATP,   0
.equ K_SP,     8
.equ K_TRAP,   16
.equ EPC,      24
.equ K_HARTID, 32

.equ RA, 40
.equ SP, 48
.equ GP, 56
.equ TP, 64
.equ T0, 72
.equ T1, 80
.equ T2, 88
.equ S0, 96
.equ S1, 104
.equ A0, 112
.equ A1, 120
.equ A2, 128
.equ A3, 136
.equ A4, 144
.equ A5, 152
.equ A6, 160
.equ A7, 168
.equ S2, 176
.equ S3, 184
.equ S4, 192
.equ S5, 200
.equ S6, 208
.equ S7, 216
.equ S8, 224
.equ S9, 232
.equ S10, 240
.equ S11, 248
.equ T3, 256
.equ T4, 264
.equ T5, 272
.equ T6, 280

trampoline:
uservec:

    csrrw a0, sscratch, a0

    # save user regs into trapframe
    sd ra, RA(a0)
    sd sp, SP(a0)
    sd gp, GP(a0)
    sd tp, TP(a0)
    sd t0, T0(a0)
    sd t1, T1(a0)
    sd t2, T2(a0)
    sd s0, S0(a0)
    sd s1, S1(a0)
    sd a1, A1(a0)
    sd a2, A2(a0)
    sd a3, A3(a0)
    sd a4, A4(a0)
    sd a5, A5(a0)
    sd a6, A6(a0)
    sd a7, A7(a0)
    sd s2, S2(a0)
    sd s3, S3(a0)
    sd s4, S4(a0)
    sd s5, S5(a0)
    sd s6, S6(a0)
    sd s7, S7(a0)
    sd s8, S8(a0)
    sd s9, S9(a0)
    sd s10, S10(a0)
    sd s11, S11(a0)
    sd t3, T3(a0)
    sd t4, T4(a0)
    sd t5, T5(a0)
    sd t6, T6(a0)

    csrr t0, sscratch
    sd t0, A0(a0)

    # save user pc
    csrr t0, sepc
    sd t0, EPC(a0)

    ld t1, K_SATP(a0)    # kernel satp
    ld sp, K_SP(a0)      # kernel stack top
    ld t2, K_TRAP(a0)    # kernel trap handler (usertrap)
    ld tp, K_HARTID(a0)  # optional, if you use tp as hartid

    csrw satp, t1
    sfence.vma zero, zero

    jr t2


userret:
    csrw satp, a0
    sfence.vma zero, zero

    csrr a0, sscratch

    ld t0, EPC(a0)
    csrw sepc, t0

    # restore regs
    ld ra, RA(a0)
    ld sp, SP(a0)
    ld gp, GP(a0)
    ld tp, TP(a0)
    ld t0, T0(a0)
    ld t1, T1(a0)
    ld t2, T2(a0)
    ld s0, S0(a0)
    ld s1, S1(a0)
    ld a1, A1(a0)
    ld a2, A2(a0)
    ld a3, A3(a0)
    ld a4, A4(a0)
    ld a5, A5(a0)
    ld a6, A6(a0)
    ld a7, A7(a0)
    ld s2, S2(a0)
    ld s3, S3(a0)
    ld s4, S4(a0)
    ld s5, S5(a0)
    ld s6, S6(a0)
    ld s7, S7(a0)
    ld s8, S8(a0)
    ld s9, S9(a0)
    ld s10, S10(a0)
    ld s11, S11(a0)
    ld t3, T3(a0)
    ld t4, T4(a0)
    ld t5, T5(a0)
    ld t6, T6(a0)

    ld a0, A0(a0)

    sret
