#pragma once
#include <stdint.h>

#define read_csr(csr)                            \
({                                               \
    uint64_t __tmp;                              \
    asm volatile ("csrr %0, " #csr               \
                  : "=r"(__tmp));                \
    __tmp;                                       \
})

#define write_csr(csr, val)                      \
do {                                             \
    uint64_t __v = (uint64_t)(val);              \
    asm volatile ("csrw " #csr ", %0"            \
                  :: "rK"(__v));                 \
} while (0)

#define swap_csr(csr, val)                       \
({                                               \
    uint64_t __tmp;                              \
    uint64_t __v = (uint64_t)(val);              \
    asm volatile ("csrrw %0, " #csr ", %1"       \
                  : "=r"(__tmp)                  \
                  : "rK"(__v));                  \
    __tmp;                                       \
})

#define set_csr_bits(csr, bits)                  \
do {                                             \
    uint64_t __v = (uint64_t)(bits);             \
    asm volatile ("csrs " #csr ", %0"            \
                  :: "rK"(__v));                 \
} while (0)

#define clear_csr_bits(csr, bits)                \
do {                                             \
    uint64_t __v = (uint64_t)(bits);             \
    asm volatile ("csrc " #csr ", %0"            \
                  :: "rK"(__v));                 \
} while (0)

//sie defines what intterrups are allowed
//sip is the current pending interrupt

#define SSTATUS_SIE      (1UL << 1) //0 = interrupts not taken in S-mode, 1 = interrupts may be taken
#define SSTATUS_SPIE     (1UL << 5) //sstatus.SIE before trapped
#define SSTATUS_SPP      (1UL << 8)

#define SIE_SSIE (1UL << 1) 
#define SIP_SSIP (1UL << 1) //is a software interrupt waiting to be handled

#define MSTATUS_MIE      (1UL << 3)
#define MSTATUS_MPIE     (1UL << 7)
#define MSTATUS_MPP_MASK (3UL << 11)
#define MSTATUS_MPP_M    (3UL << 11)
#define MSTATUS_MPP_S    (1UL << 11)
#define MSTATUS_MPP_U    (0UL << 11)



static inline uint64_t r_sstatus(void) { return read_csr(sstatus); }
static inline void     w_sstatus(uint64_t x) { write_csr(sstatus, x); }

static inline uint64_t r_stvec(void)   { return read_csr(stvec); }
static inline void     w_stvec(uint64_t x)
{
    write_csr(stvec, x & ~0x3UL);
}

static inline uint64_t r_scause(void)  { return read_csr(scause); }
static inline uint64_t r_sepc(void)    { return read_csr(sepc); }
static inline void     w_sepc(uint64_t x) { write_csr(sepc, x); }

static inline uint64_t r_stval(void)   { return read_csr(stval); }

static inline uint64_t r_satp(void)    { return read_csr(satp); }
static inline void     w_satp(uint64_t x) { write_csr(satp, x); }


static inline uint64_t r_mstatus(void) { return read_csr(mstatus); }
static inline void     w_mstatus(uint64_t x) { write_csr(mstatus, x); }

static inline uint64_t r_mepc(void)    { return read_csr(mepc); }
static inline void     w_mepc(uint64_t x) { write_csr(mepc, x); }

static inline uint64_t r_mcause(void)  { return read_csr(mcause); }

static inline uint64_t r_mtvec(void)   { return read_csr(mtvec); }
static inline void     w_mtvec(uint64_t x)
{

    write_csr(mtvec, x & ~0x3UL);
}

static inline void sstatus_enable_sie(void)
{
    set_csr_bits(sstatus, SSTATUS_SIE);
}

static inline void sstatus_disable_sie(void)
{
    clear_csr_bits(sstatus, SSTATUS_SIE);
}

/*
scause layout:
bit 63: 1/0 for interrupt/exception
lower bits = cause 
*/

static inline int scause_is_interrupt(uint64_t s) {return (int) (s >> 63);}
static inline uint64_t scause_code(uint64_t s) {return s & 0xfff;}