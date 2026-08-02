ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(5);
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = 0x400000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
