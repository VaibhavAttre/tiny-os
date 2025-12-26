#include <stdint.h>
#include "riscv.h"
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
    if(!p|| !p->tf) panic("usertrapret no proc or tf\n");

    sstatus_disable_sie();

    w_stvec(trampoline_uservec());
    
    p->tf->kernel_satp = MAKE_SATP((uint64_t)kvmpagetable());
    p->tf->kernel_sp = p->kstack_top;
    p->tf->kernel_trap = (uint64_t)usertrap;
    p->tf->kernel_hartid = r_tp();

    write_csr(sscratch, TRAPFRAME);

    uint64_t x = r_sstatus();
    x &= ~SSTATUS_SPP; //set to user mode
    x |= SSTATUS_SPIE; //enable interrupts on return to user mode
    w_sstatus(x);

    w_sepc(p->tf->epc);

    uint64_t satp = MAKE_SATP((uint64_t)p->pagetable);
    void (*fn)(uint64_t) = (void (*)(uint64_t))trampoline_userret();
    fn(satp);

    panic("usertrapret should not return\n");
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

    //TIMER INTERRUPT
    if (interrupt && exception_code == 1) {
        clear_csr_bits(sip, SIP_SSIP);
        extern void clockinterrupt();
        clockinterrupt();
        //if ((ticks % 50) == 0) kprintf("tick=%d\n", ticks);

        if(need_switch && !in_scheduler && myproc()) yield_from_trap(1);
        return;
    } 

    //SYSCALL
    if(!interrupt && (exception_code == 9 || exception_code == 8)) {

        //kprintf("ecall sepc=%p\n", (void*)sepc);
        if (tpfrm) tpfrm->epc = sepc + 4;
        else write_csr(sepc, sepc + 4);
        syscall_handler(tpfrm);
        return;
    }

    //PAGE FAULT
    if(!interrupt && (exception_code == 12|| exception_code == 13 || exception_code == 15)) {

        kprintf("Page fault code=%d sepc=%p stval=%p\n", (int)exception_code, (void*)sepc, (void*)stval);
        while(1){}
    }

    kprintf("Unhandled exception");
    while(1) {}
}

void kerneltrap() {

    trap_handler(0);
}

void usertrap() {

    w_stvec((uint64_t) kernelvec);
    struct proc * p = myproc();
    write_csr(sscratch, p->kstack_top);
    trap_handler(p->tf);
    usertrapret();    
}