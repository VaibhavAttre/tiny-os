.section .init
.globl _start
.type _start, @function

_start:
    /* Set up the stack pointer */
    la sp, __stack_top

    la t0, __bss_start
    la t1, __bss_end
    /* Zero out the .bss section */

1:  bge t0, t1, 2f
    sb zero, 0(t0)
    addi t0, t0, 1
    j 1b

2:  /* Call the main function */
    call kmain

3:
    /* Infinite loop to halt the system */
    wfi
    j 3b

    .size  _start, .-_start
    .extern __bss_start
    .extern __bss_end
    .extern __stack_top
    .extern kmain