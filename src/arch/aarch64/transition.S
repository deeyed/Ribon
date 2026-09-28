/*-
 * Copyright (c) 2012-2014 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * CurrentEL dispatch, EL2 timer/trap normalization and EL1h eret ordering are
 * adapted from FreeBSD sys/arm64/arm64/locore.S at revision
 * 9f5c4ef32812afb4573a278e6eafe5040f839d13.  Ribon deliberately omits the
 * FreeBSD VHE stay-at-EL2 path and installs its bounded identity bridge.
 */

    .text
    .p2align 4
    .global ribon_aarch64_post_exit_transition

/*
 * x0: const struct RibonAarch64PostExitTransition *
 *
 * All plan fields are loaded while firmware translation is still active.
 * The caller verified that this code, the plan and the target stack are
 * identity-addressed, so execution remains valid while stage-1 is replaced.
 */
ribon_aarch64_post_exit_transition:
    cbz     x0, .Lhalt
    ldr     x10, [x0, #0]
    ldr     x11, [x0, #8]
    ldr     x12, [x0, #16]
    ldr     x13, [x0, #24]
    ldr     x14, [x0, #32]
    ldr     x15, [x0, #40]
    ldr     x16, [x0, #48]
    ldr     w17, [x0, #56]
    cbz     x10, .Lhalt
    cbz     x14, .Lhalt
    cbz     x15, .Lhalt
    cbz     x16, .Lhalt

    msr     daifset, #0xf
    mrs     x9, CurrentEL
    lsr     x9, x9, #2
    cmp     w9, w17
    b.ne    .Lhalt
    cmp     x9, #1
    b.eq    .Lfrom_el1
    cmp     x9, #2
    b.eq    .Lfrom_el2
    b       .Lhalt

.Lfrom_el1:
    dsb     sy
    mrs     x9, sctlr_el1
    bic     x9, x9, #1
    bic     x9, x9, #4
    bic     x9, x9, #0x1000
    msr     sctlr_el1, x9
    isb
    msr     mair_el1, x12
    msr     tcr_el1, x11
    msr     ttbr0_el1, x10
    msr     ttbr1_el1, xzr
    isb
    tlbi    vmalle1
    dsb     sy
    isb
    msr     sctlr_el1, x13
    isb
    b       .Lenter_continuation

.Lfrom_el2:
    dsb     sy
    mrs     x9, sctlr_el2
    bic     x9, x9, #1
    bic     x9, x9, #4
    bic     x9, x9, #0x1000
    msr     sctlr_el2, x9
    isb

    mov     x9, #1
    lsl     x9, x9, #31
    msr     hcr_el2, x9
    mov     x9, #3
    msr     cnthctl_el2, x9
    msr     cntvoff_el2, xzr
    msr     hstr_el2, xzr
    msr     vttbr_el2, xzr

    msr     mair_el1, x12
    msr     tcr_el1, x11
    msr     ttbr0_el1, x10
    msr     ttbr1_el1, xzr
    tlbi    alle1
    dsb     sy
    isb
    msr     sctlr_el1, x13
    msr     sp_el1, x14
    adr     x9, .Lenter_continuation
    msr     elr_el2, x9
    mov     x9, #0x3c5
    msr     spsr_el2, x9
    isb
    eret

.Lenter_continuation:
    mrs     x9, CurrentEL
    cmp     x9, #4
    b.ne    .Lhalt
    msr     vbar_el1, xzr
    isb
    mov     sp, x14
    mov     x0, x16
    br      x15

.Lhalt:
    msr     daifset, #0xf
1:
    wfe
    b       1b
