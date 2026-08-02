.section .rodata
success_marker:
    .ascii "RIBON:LINUX:X86_64:PID1:v1:OK\n"
success_marker_end:
poweroff_failed:
    .ascii "RIBON:LINUX:X86_64:PID1:v1:POWEROFF-FAIL\n"
poweroff_failed_end:

.section .text
.globl _start
.type _start, @function
_start:
    mov $1, %rax
    mov $1, %rdi
    lea success_marker(%rip), %rsi
    mov $(success_marker_end - success_marker), %rdx
    syscall

    mov $169, %rax
    mov $0xfee1dead, %edi
    mov $0x28121969, %esi
    mov $0x4321fedc, %edx
    xor %r10d, %r10d
    syscall

    mov $1, %rax
    mov $1, %rdi
    lea poweroff_failed(%rip), %rsi
    mov $(poweroff_failed_end - poweroff_failed), %rdx
    syscall
1:
    hlt
    jmp 1b
.size _start, . - _start
