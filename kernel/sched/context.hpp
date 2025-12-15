#pragma once
#include "types.hpp"

namespace sched
{
    struct Context
    {
        u64 x19;
        u64 x20;
        u64 x21;
        u64 x22;
        u64 x23;
        u64 x24;
        u64 x25;
        u64 x26;
        u64 x27;
        u64 x28;
        u64 x29;
        u64 x30;   // LR
        u64 sp;
    };

    extern "C" void context_switch(Context* old_ctx, Context* next_ctx);
    extern "C" void thread_trampoline();
}
