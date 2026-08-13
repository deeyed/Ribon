OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x80400000;

    .text : ALIGN(4096) {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16) {
        KEEP(*(.rodata.fixture))
        *(.srodata .srodata.* .rodata .rodata.*)
    } :text

    .data : ALIGN(4096) {
        *(.sdata .sdata.* .data .data.*)
        PROVIDE(__global_pointer$ = . + 0x800);
    } :data

    .bss (NOLOAD) : ALIGN(16) {
        *(.sbss .sbss.* .bss .bss.*)
        *(COMMON)
    } :data

    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += 0x4000;
        __stack_top = .;
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}
