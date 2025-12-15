.section .text
.global _start
.type _start, %function

_start:
    ldr x0, =stack_top
    mov sp, x0

    ldr x1, =__bss_start
    ldr x2, =__bss_end
    mov x3, xzr

bss_clear_loop:
    cmp x1, x2
    b.hs bss_clear_done
    str x3, [x1], #8
    b bss_clear_loop

bss_clear_done:
    ldr x0, =vector_table

    mrs x1, CurrentEL
    cmp x1, #0x8
    b.eq in_el2

in_el1:
    msr VBAR_EL1, x0
    isb
    b el1_entry

in_el2:
    msr VBAR_EL2, x0
    msr VBAR_EL1, x0
    isb

    mov x2, #(1 << 31)
    msr HCR_EL2, x2
    isb

    mov x2, #3
    msr CNTHCTL_EL2, x2
    mov x2, xzr
    msr CNTVOFF_EL2, x2
    isb

    ldr x2, =stack_top
    msr SP_EL1, x2

    ldr x2, =el1_entry
    msr ELR_EL2, x2

    mov x2, #0x3C5
    msr SPSR_EL2, x2
    isb

    eret

el1_entry:
    mov x20, x0 

    ldr x0, =stack_top
    mov sp, x0 

    mrs x0, CPACR_EL1
    orr x0, x0, #(3 << 20) 
    msr CPACR_EL1, x0
    isb

    mov x0, x20 
    bl kernel_main

hang:
    wfe
    b hang

.size _start, . - _start

.section .bss
.align 4
stack:
    .skip 16384
.align 4
stack_top:
