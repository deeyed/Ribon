ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    __image_start = .;

    .text : ALIGN(4096) {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16) {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x10000;
    __stack_top = .;

    . = ALIGN(4096);
    __bootloader_runtime_end = .;

    .ribon.boot_modules : ALIGN(4096) {
        __ribon_boot_modules_start = .;
        KEEP(*(SORT_BY_NAME(.ribon.boot_modules.*)))
        . = ALIGN(4096);
        __ribon_boot_modules_end = .;
    }

    . = ALIGN(4096);
    __image_end = .;
    ASSERT(__image_end <= 0x41000000,
           "Ribon image and boot modules overlap the payload window")
}
