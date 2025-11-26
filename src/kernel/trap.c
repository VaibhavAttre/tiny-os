#include <stdint.h>
#include "riscv.h"
#include "kernel/trap.h"
#include "kernel/printf.h"

extern void trap_entry(void);

void trap_init(void) {
    
    // Set the trap handler address
    write_csr(stvec, (uintptr_t)trap_entry);
    kprintf("Trap handler initialized at address: 0x%lx\n", read_csr(stvec));
}

void trap_handler(void) {

    //scause[63] indicates whether the trap is an interrupt or an exception
    //scuase[62:0] indicates the code
    kprintf("In trap handler\n");
    uint64_t scause = read_csr(scause);
    uint64_t interrupt = scause >> 63;
    uint64_t exception_code = scause & 0xfff;
    uint64_t sepc = read_csr(sepc);
    uint64_t stval = read_csr(stval);

    kprintf("Trap occurred! scause: 0x%lx\n", scause);
    
    if (interrupt == 1) { // Interrupt
        kprintf("Interrupt occurred! Code: %lu\n", exception_code);
        kprintf("Will handle interrupts later");
        return;
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