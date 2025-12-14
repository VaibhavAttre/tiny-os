#include <stdint.h>
#include "riscv.h"
#include "kernel/trap.h"
#include "timer.h"
#include "kernel/printf.h"
#include "kernel/clock.h"

void trap_init(void) {
    
    // Set the trap handler address
    extern void trap_entry(void);
    uint64_t x = (uint64_t)trap_entry;
    w_stvec(x);
}

void trap_handler(void) {

    //scause[63] indicates whether the trap is an interrupt or an exception
    //scuase[62:0] indicates the code
    
    uint64_t scause = read_csr(scause);
    uint64_t interrupt = scause_is_interrupt(scause);
    uint64_t exception_code = scause_code(scause);
    uint64_t sepc = read_csr(sepc);
    uint64_t stval = read_csr(stval);

    //kprintf("Trap occurred! scause: 0x%lx\n", scause);
    
    if (interrupt == 1) { // Interrupt
        if(exception_code == 1)  {
                
            clear_csr_bits(sip, SIP_SSIP);

            clockinterrupt();
            return;
        }
    } else { // Exception
        kprintf("Exception occurred! Code: %lu\n", exception_code);

        if(exception_code == 9) { 
            kprintf("S mode Ecall ignoring right now: 0x%lx\n", sepc);
            write_csr(sepc, sepc + 4);
            return;
        } else {
            kprintf("Unhandled exception! sepc: 0x%lx, stval: 0x%lx\n", sepc, stval);
            while (1);
        }
    }

}