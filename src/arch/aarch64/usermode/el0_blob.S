.section .text.el0blob, "ax"
.align 4

.global el0_blob_start
.global el0_blob_end

el0_blob_start:
    mov x4, #50

1:
    cmp x0, #'B'
    b.eq 2f
    adr x2, msgA
    b 3f

2:
    adr x2, msgB

3:
    mov x0, x2
    mov x1, #1
    svc #0

    svc #2

    subs x4, x4, #1
    b.ne 1b

    mov x0, #0
    svc #1

4:
    wfe
    b 4b

msgA:
    .byte 'A'
msgB:
    .byte 'B'

.align 4
el0_blob_end:
