SECTIONS
{
    . = 0x100000;               /* Set the kernel load address */
    
    .text : {                   /* Code section */
        *(.text)
    }

    .data : {                   /* Initialized data */
        *(.data)
    }

    .bss : {                    /* Uninitialized data */
        *(.bss)
    }

    .ctors : ALIGN(4) {         /* Align the .ctors section */
        __start_ctors = .;      /* Mark the start of .ctors */
        KEEP(*(.ctors))         /* Include .ctors and ensure it's kept */
        __end_ctors = .;        /* Mark the end of .ctors */
    }
}
