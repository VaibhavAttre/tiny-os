# .gdbinit in tiny-os/

set architecture riscv:rv64

define connecttiny
    target remote localhost:1234

    break _start
    break kmain
    break trap_handler
    break mtrap

    continue
end

define dumptrap
    x/i $pc
    info symbol $pc
    p/x $mcause
    p/x $mepc
    p/x $mstatus
    p/x $scause
    p/x $sepc
end

