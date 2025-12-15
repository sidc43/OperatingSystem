#include "arch_timer.hpp"
#include "kernel/core/print.hpp"

namespace
{
    static volatile u64 g_ticks = 0;
    static u64 g_freq = 0;
    static u64 g_reload = 0;

    static u64 read_cntfrq()
    {
        u64 v;
        asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(v));
        return v;
    }

    static void set_tval(u64 ticks)
    {
        asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(ticks));
    }

    static void enable_timer()
    {
        // CNTP_CTL_EL0:
        // bit0 ENABLE=1
        // bit1 IMASK=0 (unmasked)
        u64 ctl = 1;
        asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(ctl));
        asm volatile("isb");
    }
}

namespace arch_timer
{
    void init_100hz()
    {
        g_freq = read_cntfrq();
        g_reload = (g_freq / 100);
        if (g_reload == 0)
        {
            g_reload = 1;
        }

        set_tval(g_reload);
        enable_timer();
    }

    void on_irq()
    {
        g_ticks++;
        set_tval(g_reload);

        // Don’t spam too hard
        if ((g_ticks % 50) == 0)
        {
            kprint::putc('.');
        }
    }

    u64 ticks()
    {
        return g_ticks;
    }
}
