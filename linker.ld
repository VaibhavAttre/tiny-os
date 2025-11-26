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
    
    .stack : {
        . = ALIGN(16);
        __stack_top = . + 0x4000;  
    } > RAM
}