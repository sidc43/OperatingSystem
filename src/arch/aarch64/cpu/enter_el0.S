.section .text
.align 4
.global enter_el0
.type enter_el0, %function


enter_el0:
    msr SP_EL0, x0
    msr ELR_EL1, x1

    mov x3, #0
    msr SPSR_EL1, x3
    isb

    mov x0, x2
    eret

.size enter_el0, . - enter_el0
