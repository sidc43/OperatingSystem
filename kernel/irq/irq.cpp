#include "irq.hpp"
#include "drivers/interrupt/gicv2/gicv2.hpp"
#include "drivers/timer/arch_timer.hpp"

namespace
{
    static constexpr u32 TIMER_INTID = 30;
}

namespace irq
{
    void enable()
    {
        asm volatile("msr daifclr, #2" ::: "memory");
    }

    void disable()
    {
        asm volatile("msr daifset, #2" ::: "memory");
    }

    void handle()
    {
        u32 iar = gicv2::ack();
        u32 intid = iar & 0x3FF;

        if (intid == 1023)
        {
            return;
        }

        if (intid == TIMER_INTID)
        {
            arch_timer::on_irq();
        }

        gicv2::eoi(iar);
    }
}
