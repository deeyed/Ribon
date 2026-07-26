    .section .rodata.rpi_boot_media, "a"
    .balign 16

    .global __ribon_rpi_boot_media_parus_elf_start
    .type __ribon_rpi_boot_media_parus_elf_start, %object
__ribon_rpi_boot_media_parus_elf_start:
    .incbin RIBON_RPI_KERNEL_ELF_PATH

    .global __ribon_rpi_boot_media_parus_elf_end
__ribon_rpi_boot_media_parus_elf_end:
    .balign 16

    .size __ribon_rpi_boot_media_parus_elf_start, \
        __ribon_rpi_boot_media_parus_elf_end - __ribon_rpi_boot_media_parus_elf_start

    .global __ribon_rpi_boot_media_cmdline_start
    .type __ribon_rpi_boot_media_cmdline_start, %object
__ribon_rpi_boot_media_cmdline_start:
    .incbin RIBON_RPI_CMDLINE_PATH

    .global __ribon_rpi_boot_media_cmdline_end
__ribon_rpi_boot_media_cmdline_end:
    .balign 16

    .size __ribon_rpi_boot_media_cmdline_start, \
        __ribon_rpi_boot_media_cmdline_end - __ribon_rpi_boot_media_cmdline_start
