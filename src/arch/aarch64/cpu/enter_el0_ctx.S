.section .text
.align 4

.global enter_el0_ctx
.type enter_el0_ctx, %function

enter_el0_ctx:
    mov x20, x0

    ldp x0,  x1,  [x20, #0]
    ldp x2,  x3,  [x20, #16]
    ldp x4,  x5,  [x20, #32]
    ldp x6,  x7,  [x20, #48]
    ldp x8,  x9,  [x20, #64]
    ldp x10, x11, [x20, #80]
    ldp x12, x13, [x20, #96]
    ldp x14, x15, [x20, #112]
    ldp x16, x17, [x20, #128]
    ldp x18, x19, [x20, #144]

    ldp x21, x22, [x20, #168]
    ldp x23, x24, [x20, #184]
    ldp x25, x26, [x20, #200]
    ldp x27, x28, [x20, #216]
    ldp x29, x30, [x20, #232]

    ldr x9,  [x20, #248]   
    ldr x10, [x20, #256]    

    ldr x20, [x20, #160]    

    msr SP_EL0, x9
    msr ELR_EL1, x10

    mov x11, #0x3C0
    msr SPSR_EL1, x11
    isb

    eret
