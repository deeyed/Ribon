    .section .text.entry, "ax"
    .balign 16
    .global _start
    .type _start, %function

_start:
    mov x20, x0
    adrp x1, __bss_start
    add x1, x1, :lo12:__bss_start
    adrp x2, __bss_end
    add x2, x2, :lo12:__bss_end
1:
    cmp x1, x2
    b.hs 2f
    str xzr, [x1], #8
    b 1b
2:
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1
    mov x1, x20
    mov x0, xzr
    bl ribon_raw_fdt_boot_main
3:
    wfe
    b 3b

    .size _start, . - _start
