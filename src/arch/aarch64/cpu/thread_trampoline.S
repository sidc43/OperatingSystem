.global thread_trampoline
.type thread_trampoline, %function

.extern thread_exit

thread_trampoline:
    mov x0, x20 
    blr x19 
    bl thread_exit 
