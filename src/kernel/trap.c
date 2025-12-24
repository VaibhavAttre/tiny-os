#include <stdint.h>
#include "riscv.h"
#include "kernel/trap.h"
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/clock.h"
#include "kernel/sched.h"
#include "kernel/current.h"

void trap_init(void) {
    
    // Set the trap handler address
    extern void trap_entry(void);
    uint64_t x = (uint64_t)trap_entry;
    w_stvec(x);
}

void trap_handler(struct trapframe * tpfrm) {

    //scause[63] indicates whether the trap is an interrupt or an exception
    //scuase[62:0] indicates the code
    
    uint64_t scause = read_csr(scause);
    uint64_t interrupt = scause_is_interrupt(scause);
    uint64_t exception_code = scause_code(scause);
    uint64_t sepc = tpfrm ? tpfrm->sepc : read_csr(sepc);
    uint64_t stval = read_csr(stval);

    //kprintf("Trap occurred! scause: %p\n", (void*)scause);    
    if (interrupt && exception_code == 1) {
        clear_csr_bits(sip, SIP_SSIP);
        clockinterrupt();
        //if ((ticks % 50) == 0) kprintf("tick=%d\n", ticks);
        
        if(need_switch && !in_scheduler && myproc()) yield();
        return;
    } 
    if(!interrupt && exception_code == 9) { // S mode ecall

        kprintf("ecall sepc=%p\n", (void*)sepc);
        if (tpfrm) tpfrm->sepc = sepc + 4;
        else write_csr(sepc, sepc + 4);
        return;
    }

    if(!interrupt && (exception_code == 12|| exception_code == 13 || exception_code == 15)) {

        kprintf("Page fault code=%d sepc=%p stval=%p\n", (int)exception_code, (void*)sepc, (void*)stval);
        while(1){}
    }

    kprintf("Unhandled exception");
    while(1) {}
}