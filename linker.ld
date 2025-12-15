ENTRY(_start)

SECTIONS
{
  . = 0x40100000;

  __kernel_start = .;

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }

  .bss :
  {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }

  __kernel_end = .;
}
