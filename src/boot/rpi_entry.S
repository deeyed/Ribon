    .section .text.boot, "ax"
    .balign 8
    .global _start
    .type _start, %function

_start:
    b ribon_rpi_reset
    .long 0
    .quad 0x00080000
    .quad __image_end - _start
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .long 0x644d5241
    .long 0

ribon_rpi_reset:
    mov x8, x0
    mov x9, x1
    mov x10, x2
    mov x11, x3
    mov x13, sp
    mrs x12, CurrentEL

    adrp x4, __bss_start
    add x4, x4, :lo12:__bss_start
    adrp x5, __bss_end
    add x5, x5, :lo12:__bss_end
1:
    cmp x4, x5
    b.hs 2f
    str xzr, [x4], #8
    b 1b

2:
    adrp x4, __stack_top
    add x4, x4, :lo12:__stack_top
    mov sp, x4

    mov x0, x8
    mov x1, x9
    mov x2, x10
    mov x3, x11
    mov x4, x12
    mov x5, x13
    bl ribon_rpi_payload_entry

3:
    wfe
    b 3b

    .size _start, . - _start

    .global ribon_rpi_prepare_handoff_state
    .type ribon_rpi_prepare_handoff_state, %function
ribon_rpi_prepare_handoff_state:
    msr daifset, #0xf
    dsb sy
    isb
    ret
    .size ribon_rpi_prepare_handoff_state, . - ribon_rpi_prepare_handoff_state

    .global ribon_rpi_enter_kernel
    .type ribon_rpi_enter_kernel, %function
ribon_rpi_enter_kernel:
    mov x16, x0
    mov x0, x1
    mov x1, x2
    br x16
    .size ribon_rpi_enter_kernel, . - ribon_rpi_enter_kernel
