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

    /*
    // Set the trap handler address
    extern void trap_entry(void);
    uint64_t x = (uint64_t)trap_entry;
    w_stvec(x);
    
    // "trap stack" pointer. Before the scheduler starts, use the
    // current boot stack so early timer interrupts can't explode.
    uint64_t sp;
    asm volatile("mv %0, sp" : "=r"(sp));
    write_csr(sscratch, sp);*/

}

void trap_handler(struct trapframe * tpfrm) {

    //scause[63] indicates whether the trap is an interrupt or an exception
    //scuase[62:0] indicates the code
    
    uint64_t scause = read_csr(scause);
    uint64_t interrupt = scause_is_interrupt(scause);
    uint64_t exception_code = scause_code(scause);
    uint64_t sepc = tpfrm ? tpfrm->epc : read_csr(sepc);
    uint64_t stval = read_csr(stval);

    int from_user = (tpfrm != 0);

    //TIMER INTERRUPT
    if (interrupt && exception_code == 1) {
        clear_csr_bits(sip, SIP_SSIP);
        extern void clockinterrupt();
        clockinterrupt();
        //if ((ticks % 50) == 0) kprintf("tick=%d\n", ticks);

        if(from_user && need_switch && !in_scheduler && myproc()) yield_from_trap(1);
        return;
    } 

    //SYSCALL FROM USER MODE
    if(from_user && !interrupt && exception_code == 8) {

        //kprintf("ecall sepc=%p\n", (void*)sepc);
        if (tpfrm) tpfrm->epc = sepc + 4;
        else write_csr(sepc, sepc + 4);
        sstatus_enable_sie();
        syscall_handler(tpfrm);
        // Skip post-syscall killed check here to avoid deref on a bad curr.
        return;
    }

    //PAGE FAULT
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
