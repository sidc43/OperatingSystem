.section .text
.align 11
.global vector_table

.macro VEC id
    mov x0, #\id
    b vector_common
.endm

vector_table:
    VEC 0
    .balign 0x80
    VEC 1
    .balign 0x80
    VEC 2
    .balign 0x80
    VEC 3
    .balign 0x80

    VEC 4
    .balign 0x80
    VEC 5
    .balign 0x80
    VEC 6
    .balign 0x80
    VEC 7
    .balign 0x80

    VEC 8
    .balign 0x80
    VEC 9
    .balign 0x80
    VEC 10
    .balign 0x80
    VEC 11
    .balign 0x80

    VEC 12
    .balign 0x80
    VEC 13
    .balign 0x80
    VEC 14
    .balign 0x80
    VEC 15
    .balign 0x80

.global vector_common
.type vector_common, %function
vector_common:
    sub sp, sp, #272


    stp x0,  x1, [sp, #0]
    stp x2,  x3, [sp, #16]
    stp x4,  x5, [sp, #32]
    stp x6,  x7, [sp, #48]
    stp x8,  x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]


    mrs x5, CurrentEL
    cmp x5, #0x8
    b.eq 1f

    mrs x1, ESR_EL1
    mrs x2, ELR_EL1
    mrs x3, FAR_EL1
    b 2f

1:
    mrs x1, ESR_EL2
    mrs x2, ELR_EL2
    mrs x3, FAR_EL2

2:
    mov x4, sp
    bl exception_dispatch

    mov sp, x0

    ldr x30, [sp, #240]
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8,  x9,  [sp, #64]
    ldp x6,  x7,  [sp, #48]
    ldp x4,  x5,  [sp, #32]
    ldp x2,  x3,  [sp, #16]
    ldp x0,  x1,  [sp, #0]

    add sp, sp, #272
    eret

.size vector_common, . - vector_common
