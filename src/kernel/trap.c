#include <stdint.h>
#include "riscv.h"
#include "sv39.h"
#include "kernel/trap.h"
#include "kernel/printf.h"
#include "kernel/current.h"
#include "kernel/memlayout.h"
#include "kernel/vm.h"
#include "kernel/sched.h"
#include "kernel/syscall.h"

extern char trampoline[], uservec[], userret[];
extern void kernelvec();

static inline uint64_t trampoline_uservec() {
    return TRAMPOLINE + (uint64_t)(uservec - trampoline);
}

static inline uint64_t trampoline_userret() {
    return TRAMPOLINE + (uint64_t)(userret - trampoline);
}

void usertrapret() {

    struct proc * p = myproc();
    if(!p|| !p->tf) {
        kprintf("usertrapret no proc or tf (p=%p curr=%p sp=%p)\n",
                p, getmyproc(), (void*)read_sp());
        panic("usertrapret no proc or tf\n");
    }

    sstatus_disable_sie();

    w_stvec(trampoline_uservec());

    p->tf->kernel_satp = MAKE_SATP((uint64_t)kvmpagetable());
    p->tf->kernel_sp = p->kstack_top;
    p->tf->kernel_trap = (uint64_t)usertrap;
    p->tf->kernel_hartid = r_tp();

    w_sepc(p->tf->epc);

    uint64_t x = r_sstatus();
    x &= ~SSTATUS_SPP; //set to user mode
    x &= ~SSTATUS_SIE;
    x |= SSTATUS_SPIE; //enable interrupts on return to user mode
    w_sstatus(x);

    write_csr(sscratch, TRAPFRAME);

    uint64_t satp = MAKE_SATP((uint64_t)p->pagetable);
    uint64_t fn = trampoline_userret();

    ((void(*)(uint64_t))fn)(satp);
    __builtin_unreachable();
}

void trap_init(void) {

    extern void kernelvec();
    uint64_t x = (uint64_t)kernelvec;
    w_stvec(x);

}

void trap_handler(struct trapframe * tpfrm) {

    uint64_t scause = read_csr(scause);
    uint64_t interrupt = scause_is_interrupt(scause);
    uint64_t exception_code = scause_code(scause);
    uint64_t sepc = tpfrm ? tpfrm->epc : read_csr(sepc);
    uint64_t stval = read_csr(stval);

    int from_user = (tpfrm != 0);

    if (interrupt && exception_code == 1) {
        clear_csr_bits(sip, SIP_SSIP);
        extern void clockinterrupt();
        clockinterrupt();

        if(from_user && need_switch && !in_scheduler && myproc()) yield_from_trap(1);
        return;
    }

    if(from_user && !interrupt && exception_code == 8) {

        if (tpfrm) tpfrm->epc = sepc + 4;
        else write_csr(sepc, sepc + 4);
        sstatus_enable_sie();
        syscall_handler(tpfrm);
        return;
    }

    if(!interrupt && (exception_code == 12|| exception_code == 13 || exception_code == 15)) {

        if(from_user) {
            struct proc * p = myproc();
            proc_kill(p, -1);
            kprintf("proc %d killed due to page fault\n", p->id);
            kprintf("PF pid=%d code=%d sepc=%p stval=%p\n", p->id, (int)exception_code, (void*)sepc, (void*)stval);

            return;
        }
    }

    if(from_user) {

        struct proc * p = myproc();
        proc_kill(p, -1);
        kprintf("proc %d killed due to page fault\n", p->id);
        return;
    }

    kprintf("Unhandled exception scause=%p sepc=%p stval=%p\n",
            (void*)scause, (void*)sepc, (void*)stval);
    panic("unhandledc trap\n");
}

void kerneltrap() {

    trap_handler(0);
}

void usertrap() {

    w_stvec((uint64_t) kernelvec);
    struct proc * p = myproc();
    trap_handler(p->tf);
    if (p && p->killed) {
        proc_exit(p->exit_status ? p->exit_status : -1);
    }
    usertrapret();
}
