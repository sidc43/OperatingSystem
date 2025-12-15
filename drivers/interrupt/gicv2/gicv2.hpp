#pragma once
#include "types.hpp"

namespace gicv2
{
    void init();
    void enable_int(u32 intid);

    u32 ack();
    void eoi(u32 intid);
}
