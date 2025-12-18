MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{   
    . = ORIGIN(RAM);

    __kernel_start = .;
    .text : ALIGN(4096) {
        __text_start = .;
        *(.text._start)
        *(.text*)
        . = ALIGN(4096);
        __text_end = .;
    } > RAM

    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata*) *(.srodata*)
        . = ALIGN(4096);
        __rodata_end = .;
    } > RAM

    .data : ALIGN(4096) {
        __data_start = .;
        *(.data*) *(.sdata*)
        . = ALIGN(4096);
        __data_end = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(4096) {
        __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(4096) {
        __stack_bottom = .;
        . += 0x4000;
        __stack_top = .;
    } > RAM
    
    __kernel_end = .;
    _end = .;
}