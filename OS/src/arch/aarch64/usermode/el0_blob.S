.section .text.el0blob, "ax"
.align 4

.global el0_blob_start
.global el0_blob_end

el0_blob_start:
    mov x0, #41
    mov x1, #10

1:
    svc #0
    subs x1, x1, #1
    b.ne 1b

    svc #1

2:
    wfe
    b 2b

el0_blob_end:
