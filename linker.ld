MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{
    .text : {
        *(.text._start)
        *(.text*)
    } > RAM
    
    .data : { *(.data*) } > RAM
    .bss : { 
        . = ALIGN(16);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM
    
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 0x4000;
        __stack_top = .;
    } > RAM
}

_end = .;