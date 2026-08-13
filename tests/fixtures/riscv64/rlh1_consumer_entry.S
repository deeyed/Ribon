    .section .text.entry, "ax"
    .align 4
    .global _start
    .type _start, @function

_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      sp, __stack_top
    andi    sp, sp, -16
    call    ribon_rlh1_fixture_main
1:
    wfi
    j       1b

    .size _start, . - _start
