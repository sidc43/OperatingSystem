#pragma once
#include "types.hpp"

namespace arch_timer
{
    void init_100hz();
    void on_irq();
    u64  ticks();
}
