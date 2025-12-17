    .section .text
    .align 4

    .global enter_el0
    .type enter_el0, %function

    .extern g_el0_return_pc

    enter_el0:
        stp x29, x30, [sp, #-16]!
        mov x29, sp

        adrp x3, g_el0_return_pc
        add  x3, x3, :lo12:g_el0_return_pc

        adrp x4, enter_el0_return
        add  x4, x4, :lo12:enter_el0_return
        str  x4, [x3]

        msr SP_EL0, x0
        msr ELR_EL1, x1

        mov x5, #0
        msr SPSR_EL1, x5
        isb

        mov x0, x2

        eret

    enter_el0_return:
        ldp x29, x30, [sp], #16
        ret
