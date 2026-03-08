/*
  boot.S - aarch64 kernel entry point
  qemu -kernel jumps here at EL2
  sets up the stack, zeroes bss, drops to EL1, installs vectors, calls kernel_main
*/
.section .text.boot, "ax"
.global _start

.extern __stack_top
.extern __bss_start
.extern __bss_end
.extern vectors
.extern kernel_main

_start:

    mov  x19, x0

    ldr  x0, =__stack_top
    mov  sp, x0

    ldr  x1, =__bss_start
    ldr  x2, =__bss_end
.Lbss_loop:
    cmp  x1, x2
    b.ge .Lbss_done
    str  xzr, [x1], #8
    b    .Lbss_loop
.Lbss_done:

    mrs  x0, CurrentEL
    lsr  x0, x0, #2
    cmp  x0, #2
    b.ne .Lel1_entry

.Lel2_drop:

    mov  x0, #(1 << 31)
    msr  HCR_EL2, x0
    isb

    msr  CPTR_EL2, xzr
    msr  MDCR_EL2, xzr
    isb

    ldr  x0, =__stack_top
    msr  SP_EL1, x0

    msr  SCTLR_EL1, xzr
    isb

    mov  x0, #(0x5 | 0x1C0)
    msr  SPSR_EL2, x0

    adr  x0, .Lel1_entry
    msr  ELR_EL2, x0
    isb
    eret

.Lel1_entry:

    msr  SCTLR_EL1, xzr
    isb

    mov  x0, #(3 << 20)
    msr  CPACR_EL1, x0
    isb

    ldr  x0, =vectors
    msr  VBAR_EL1, x0
    isb

    mov  x0, x19
    bl   kernel_main

.Lhalt:
    wfe
    b    .Lhalt
