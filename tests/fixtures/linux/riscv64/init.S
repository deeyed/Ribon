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
.type _start, @function
_start:
    li      a0, 1
    la      a1, success_marker
    li      a2, 23
    li      a7, 64
    ecall

    li      a0, 0xfee1dead
    li      a1, 0x28121969
    li      a2, 0x4321fedc
    li      a3, 0
    li      a7, 142
    ecall

    li      a0, 1
    la      a1, poweroff_failed
    li      a2, 34
    li      a7, 64
    ecall
1:
    wfi
    j       1b
.size _start, . - _start
