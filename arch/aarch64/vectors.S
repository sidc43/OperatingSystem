/*
  vectors.S - aarch64 EL1 exception vector table
  aligned to 2048 bytes as required by the arm spec
  kernel only uses the SP_ELx bank (sync fault + irq)
  each entry saves all registers then calls the c handler in exceptions.cpp
*/
.extern sync_entry
.extern irq_entry

.macro save_regs
    sub  sp, sp, #272
    stp  x0,  x1,  [sp,   #0]
    stp  x2,  x3,  [sp,  #16]
    stp  x4,  x5,  [sp,  #32]
    stp  x6,  x7,  [sp,  #48]
    stp  x8,  x9,  [sp,  #64]
    stp  x10, x11, [sp,  #80]
    stp  x12, x13, [sp,  #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]
    mrs  x20, elr_el1
    mrs  x21, spsr_el1
    stp  x30, x20, [sp, #240]
    str  x21,      [sp, #256]
.endm

.macro restore_regs
    ldr  x21,      [sp, #256]
    ldp  x30, x20, [sp, #240]
    msr  spsr_el1, x21
    msr  elr_el1,  x20
    ldp  x28, x29, [sp, #224]
    ldp  x26, x27, [sp, #208]
    ldp  x24, x25, [sp, #192]
    ldp  x22, x23, [sp, #176]
    ldp  x20, x21, [sp, #160]
    ldp  x18, x19, [sp, #144]
    ldp  x16, x17, [sp, #128]
    ldp  x14, x15, [sp, #112]
    ldp  x12, x13, [sp,  #96]
    ldp  x10, x11, [sp,  #80]
    ldp  x8,  x9,  [sp,  #64]
    ldp  x6,  x7,  [sp,  #48]
    ldp  x4,  x5,  [sp,  #32]
    ldp  x2,  x3,  [sp,  #16]
    ldp  x0,  x1,  [sp,   #0]
    add  sp, sp, #272
.endm

.section .text, "ax"
.global _sync_handler
_sync_handler:
    save_regs
    mrs  x0, esr_el1
    mrs  x1, far_el1
    mrs  x2, elr_el1
    bl   sync_entry
    restore_regs
    eret

.global _irq_handler
_irq_handler:
    save_regs
    bl   irq_entry
    restore_regs
    eret

.global _default_handler
_default_handler:
    save_regs
    mrs  x0, esr_el1
    mrs  x1, far_el1
    mrs  x2, elr_el1
    bl   sync_entry
    restore_regs
    eret

.section .text.vectors, "ax"
.balign 2048
.global vectors
vectors:

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _sync_handler

.balign 128
    b _irq_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler

.balign 128
    b _default_handler
