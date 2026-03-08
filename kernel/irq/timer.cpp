/*
  timer.cpp - arm generic timer at 100hz
  uses cntp_tval_el0 (physical non-secure) which fires at gic ppi 30
  increments a tick counter each irq. one tick = 10ms
*/
#include "kernel/irq/timer.hpp"
#include "kernel/irq/gic.hpp"
#include "kernel/core/print.hpp"
#include "arch/aarch64/regs.hpp"
#include <stdint.h>

namespace timer {

static constexpr uint32_t TIMER_IRQ = 30;

static constexpr uint64_t CTL_ENABLE  = (1u << 0);
[[maybe_unused]] static constexpr uint64_t CTL_IMASK   = (1u << 1);
[[maybe_unused]] static constexpr uint64_t CTL_ISTATUS = (1u << 2);

static uint64_t          g_ticks_per_irq = 0;
static uint64_t volatile g_tick_count    = 0;

static void on_tick() {
    g_tick_count = g_tick_count + 1;

    write_cntp_tval_el0(g_ticks_per_irq);
}

void init(uint32_t hz) {
    uint64_t freq = read_cntfrq_el0();
    if (freq == 0) {
        print("timer: CNTFRQ is 0 – cannot initialise\n");
        return;
    }

    g_ticks_per_irq = freq / hz;

    printk("timer: freq=%u Hz  ticks_per_irq=%u  target_hz=%u\n",
           (unsigned)freq, (unsigned)g_ticks_per_irq, (unsigned)hz);

    gic::register_handler(TIMER_IRQ, on_tick);
    gic::set_priority(TIMER_IRQ, 0x40);
    gic::enable_irq(TIMER_IRQ);

    write_cntp_tval_el0(g_ticks_per_irq);
    write_cntp_ctl_el0(CTL_ENABLE);
    isb();

    print("timer: init done\n");
}

uint64_t ticks() {
    return g_tick_count;
}

void sleep_ms(uint32_t ms) {
    uint64_t freq    = read_cntfrq_el0();
    uint64_t target  = read_cntpct_el0() + (freq / 1000) * ms;
    while (read_cntpct_el0() < target) {
        asm volatile("nop");
    }
}

}
