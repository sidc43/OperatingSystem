.section .text.el0_yield_blob, "ax"
.align 4

.global el0_yield_blob_start
.global el0_yield_blob_end

el0_yield_blob_start:
    sub sp, sp, #16
    strb w0, [sp]

1:
    mov x8, #64
    mov x0, #1
    mov x1, sp
    mov x2, #1
    svc #0

    mov x8, #124
    svc #0

    b 1b

el0_yield_blob_end:
