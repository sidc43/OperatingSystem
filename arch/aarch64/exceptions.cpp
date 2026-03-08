/*
  exceptions.cpp - c-level exception handlers called from vectors.S
  handles synchronous exceptions (prints esr/far/elr and panics)
  and irq dispatch (calls gic::dispatch)
*/
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"
#include "kernel/irq/gic.hpp"
#include <stdint.h>

extern "C" void sync_entry(uint64_t esr, uint64_t far, uint64_t elr) {
    uint32_t ec  = (esr >> 26) & 0x3F;
    uint32_t iss = (uint32_t)(esr & 0x00FFFFFFu);

    print("\n\n*** KERNEL PANIC: Synchronous Exception ***\n");
    print("  ELR (faulting PC):   0x"); print_hex(elr); print("\n");
    print("  FAR (faulting addr): 0x"); print_hex(far); print("\n");
    print("  ESR: 0x"); print_hex(esr); print("\n");
    printk("  EC=0x%x (%s)  ISS=0x%x\n", ec,
           ec == 0x04 ? "EL1 Stack Abort" :
           ec == 0x15 ? "SVC"  :
           ec == 0x21 ? "Insn Abort EL0" :
           ec == 0x25 ? "Data Abort EL1" :
           ec == 0x22 ? "PC Align" :
           ec == 0x26 ? "SP Align" :
           ec == 0x07 ? "FP/SIMD trap" : "other",
           iss);

    for (;;) asm volatile("wfe");
}

extern "C" void irq_entry() {
    gic::dispatch();
}
