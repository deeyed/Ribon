.section .rodata
.balign 8
success_marker:
    .ascii "RIBON:LINUX:PID1:v1:OK\n"
success_marker_end:
poweroff_failed:
    .ascii "RIBON:LINUX:PID1:v1:POWEROFF-FAIL\n"
poweroff_failed_end:

.section .text
.globl _start
.type _start, %function
_start:
    mov x0, #1
    adrp x1, success_marker
    add x1, x1, :lo12:success_marker
    mov x2, #(success_marker_end - success_marker)
    mov x8, #64
    svc #0

    movz w0, #0xdead
    movk w0, #0xfee1, lsl #16
    movz w1, #0x1969
    movk w1, #0x2812, lsl #16
    movz w2, #0xfedc
    movk w2, #0x4321, lsl #16
    mov x3, #0
    mov x8, #142
    svc #0

    mov x0, #1
    adrp x1, poweroff_failed
    add x1, x1, :lo12:poweroff_failed
    mov x2, #(poweroff_failed_end - poweroff_failed)
    mov x8, #64
    svc #0
1:
    wfe
    b 1b
.size _start, . - _start
