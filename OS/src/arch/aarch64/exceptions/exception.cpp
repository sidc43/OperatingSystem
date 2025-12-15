#include "types.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/irq/irq.hpp"

extern "C" u64 g_el0_return_pc;
extern "C" u64 g_el0_result;

namespace
{
    struct TrapFrame
    {
        u64 x[31]; // x0..x30
    };

    static inline u64 current_el()
    {
        u64 v;
        asm volatile("mrs %0, CurrentEL" : "=r"(v));
        return (v >> 2) & 3;
    }

    static inline u64 esr_ec(u64 esr)
    {
        return (esr >> 26) & 0x3Fu;
    }

    static inline void set_elr(u64 v)
    {
        // In your banner you saw CurrentEL=1, so ELR_EL1 is the correct one.
        asm volatile("msr ELR_EL1, %0" :: "r"(v));
        asm volatile("isb");
    }

    static inline void set_spsr(u64 v)
    {
        asm volatile("msr SPSR_EL1, %0" :: "r"(v));
        asm volatile("isb");
    }
}

extern "C" void exception_dispatch(u64 vector_id, u64 esr, u64 elr, u64 far, void* frame)
{
    // IRQ vectors
    if (vector_id == 1 || vector_id == 5 || vector_id == 9 || vector_id == 13)
    {
        irq::handle();
        return;
    }

    // SVC64: EC = 0x15
    if (esr_ec(esr) == 0x15)
    {
        static bool banner = false;
        if (!banner)
        {
            banner = true;
            kprint::puts("\n[exception] SVC handler ACTIVE. CurrentEL=");
            kprint::dec_u64(current_el());
            kprint::puts("\n");
        }

        TrapFrame* tf = (TrapFrame*)frame;
        u64 sysno = (esr & 0xFFFFu);

        if (sysno == 0)
        {
            // svc #0: increment x0 and return
            kprint::putc('s');
            tf->x[0] = tf->x[0] + 1;

            // IMPORTANT: do NOT elr+4 here on your setup (it skips the subs)
            // Just return; ELR already resumes at the correct next instruction.
            (void)elr;
            return;
        }

        if (sysno == 1)
        {
            kprint::puts("\n[svc #1] returning to kernel...\n");
            g_el0_result = tf->x[0];

            // Return to EL1h
            set_spsr(5);
            set_elr(g_el0_return_pc);
            return;
        }

        // Unknown syscall: just return
        tf->x[0] = (u64)-1;
        return;
    }

    kprint::puts("\n=== EXCEPTION ===\n");
    kprint::puts("vector_id: "); kprint::dec_u64(vector_id);
    kprint::puts("\nCurrentEL: "); kprint::dec_u64(current_el());
    kprint::puts("\nESR: ");      kprint::hex_u64(esr);
    kprint::puts("\nELR: ");      kprint::hex_u64(elr);
    kprint::puts("\nFAR: ");      kprint::hex_u64(far);
    kprint::puts("\n");
    panic("exception");
}
