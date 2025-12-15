#include "kernel/tests/sched_tests.hpp"
#include "kernel/sched/sched.hpp"
#include "kernel/sched/thread.hpp"
#include "kernel/core/print.hpp"

namespace
{
    void hog_a(void*)
    {
        u64 i = 0;
        while (1)
        {
            // Intentionally do NOT call yield.
            for (volatile u64 k = 0; k < 15000000; k++) {}

            kprint::puts("[A] i=");
            kprint::dec_u64(i++);
            kprint::puts("\n");
        }
    }

    void hog_b(void*)
    {
        u64 i = 0;
        while (1)
        {
            for (volatile u64 k = 0; k < 15000000; k++) {}

            kprint::puts("[B] i=");
            kprint::dec_u64(i++);
            kprint::puts("\n");
        }
    }
}

namespace tests
{
    void scheduler_preempt_test()
    {
        sched::init();
        sched::add(sched::create_thread(hog_a, nullptr));
        sched::add(sched::create_thread(hog_b, nullptr));

        // If preemption works, you will see BOTH [A] and [B] prints
        // even though neither thread yields.
        sched::start();
    }
}
