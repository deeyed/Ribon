    .section .text.entry, "ax"
    .align 4
    .global _start
    .type _start, @function

_start:
    csrci   sstatus, 2
    csrw    sie, zero
    csrw    satp, zero
    sfence.vma zero, zero
    mv      s2, a0
    mv      s3, a1

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      t0, __bss_start
    la      t1, __bss_end
1:
    bgeu    t0, t1, 2f
    sb      zero, 0(t0)
    addi    t0, t0, 1
    j       1b
2:
    la      sp, __stack_top
    andi    sp, sp, -16
    mv      a0, s2
    mv      a1, s3
    call    ribon_raw_fdt_boot_main
3:
    wfi
    j       3b

    .size _start, . - _start
