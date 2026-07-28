OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;
    __image_start = .;

    .text : ALIGN(4096) {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.srodata .srodata.* .rodata .rodata.*)
    }

    .data : ALIGN(16) {
        *(.sdata .sdata.* .data .data.*)
        PROVIDE(__global_pointer$ = . + 0x800);
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000;
    __stack_top = .;

    . = ALIGN(4096);
    __image_end = .;
    ASSERT(__image_end <= 0x80400000,
           "Ribon image overlaps the Parus payload window")

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}
