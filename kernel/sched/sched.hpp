#pragma once
#include "types.hpp"
#include "kernel/sched/thread.hpp"

namespace sched
{
    void init();
    void add(Thread* t);

    void yield();
    void start();

    // Called from exception/IRQ path:
    // - stores current thread’s saved_frame + saved_elr
    // - picks next runnable
    // - sets ELR_EL1 to next->saved_elr
    // - returns next->saved_frame for vectors.S to restore
    void* on_irq(void* frame, u64 elr);

    void on_thread_exit();
}
