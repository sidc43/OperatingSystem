#include "src/arch/aarch64/exceptions/exception.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/irq/irq.hpp"
#include "kernel/syscall/syscall.hpp"

namespace
{
    static inline u64 current_el()
    {
        u64 v;
        asm volatile("mrs %0, CurrentEL" : "=r"(v));
        return (v >> 2) & 3;
    }

    static inline u64 esr_ec(u64 esr) { return (esr >> 26) & 0x3Fu; }

    static inline void irq_disable()
    {
        asm volatile("msr daifset, #2" ::: "memory");
    }
}

extern "C" void exception_dispatch(u64 vector_id, u64 esr, u64 elr, u64 far, void* frame)
{
    // IRQ vectors (cover SP0/SPx and lower EL variants)
    if (vector_id == 1 || vector_id == 5 || vector_id == 9 || vector_id == 13)
    {
        irq::handle();
        return;
    }

    // EL0 synchronous
    if (vector_id == 8)
    {
        // SVC64 => syscall layer
        if (esr_ec(esr) == 0x15)
        {
            (void)syscall::handle_svc(esr, elr, frame);
            return;
        }

        // Any other EL0 sync => dump once and stop (prevents recursive trash output)
        irq_disable();
        kprint::puts("\n=== EXCEPTION (EL0 sync, non-SVC) ===\n");
        kprint::puts("CurrentEL: "); kprint::dec_u64(current_el()); kprint::puts("\n");
        kprint::puts("vector_id: "); kprint::dec_u64(vector_id); kprint::puts("\n");
        kprint::puts("ESR: "); kprint::hex_u64(esr); kprint::puts("\n");
        kprint::puts("ELR: "); kprint::hex_u64(elr); kprint::puts("\n");
        kprint::puts("FAR: "); kprint::hex_u64(far); kprint::puts("\n");
        panic("EL0 fault");
    }

    irq_disable();
    kprint::puts("\n=== EXCEPTION ===\n");
    kprint::puts("CurrentEL: "); kprint::dec_u64(current_el()); kprint::puts("\n");
    kprint::puts("vector_id: "); kprint::dec_u64(vector_id); kprint::puts("\n");
    kprint::puts("ESR: "); kprint::hex_u64(esr); kprint::puts("\n");
    kprint::puts("ELR: "); kprint::hex_u64(elr); kprint::puts("\n");
    kprint::puts("FAR: "); kprint::hex_u64(far); kprint::puts("\n");
    panic("exception");
}
