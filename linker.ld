/* ────────────────────────────────────────────────────────────────────────────
   Linker script for AArch64 bare-metal kernel on QEMU virt machine.
   QEMU loads the ELF at 0x40000000 and passes DTB addr in x0.
   ──────────────────────────────────────────────────────────────────────────── */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    /* ── Code ──────────────────────────────────────────────────────────────── */
    .text : {
        KEEP(*(.text.boot))     /* boot stub must come first */
        *(.text .text.*)
    }

    . = ALIGN(4096);

    /* ── Read-only data ─────────────────────────────────────────────────────── */
    .rodata : { *(.rodata .rodata.*) }

    . = ALIGN(4096);

    /* ── Writable data ──────────────────────────────────────────────────────── */
    .data : { *(.data .data.*) }

    . = ALIGN(16);

    /* ── BSS (zero-initialised by boot stub) ────────────────────────────────── */
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    /* ── Heap (64 MiB, NOLOAD – not written to ELF file) ───────────────────── */
    /* Phase 2 needs ~4-6 MiB extra for virtio-gpu framebuffer + rings.       */
    .heap (NOLOAD) : {
        __heap_start = .;
        . += 64M;
        __heap_end   = .;
    }

    /* ── Guard page (Phase 8: MMU maps this as no-access to catch overflow) ── */
    .guard_page (NOLOAD) : {
        . = ALIGN(4096);
        __guard_start = .;
        . += 4096;
        __guard_end   = .;
    }

    /* ── Stack (64 KiB, NOLOAD) ─────────────────────────────────────────────── */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 64K;
        __stack_top = .;        /* SP starts here and grows down */
    }
}
