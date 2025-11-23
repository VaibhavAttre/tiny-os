


ENTRY(_start)

SECTIONS {

    
    . = 0x80000000;

    .text : ALIGN(4K) {
        
        *(.init)
        *(.text .text.*)
    }

    .bss : ALIGN(4K) {
        
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;

        . = ALIGN(16);
        __stack_top = . + 16K;
    }

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K) {
        
        *(.data .data.*)
    }
}
