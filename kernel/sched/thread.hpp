#pragma once
#include "types.hpp"
#include "kernel/sched/context.hpp"
#include "kernel/arch/aarch64/trapframe.hpp"

namespace sched
{
    enum class ThreadState : u32
    {
        Runnable,
        Exited
    };

    using ThreadEntry = void(*)(void*);

    struct Thread
    {
        // U1 cooperative switch context (still used for bootstrap start)
        Context ctx {};

        void*   stack_base {};
        usize   stack_size {};

        // U2 preemption: saved trapframe + saved ELR
        void*   saved_frame {};   // points to base of main frame (272 bytes)
        u64     saved_elr {};     // PC to resume at (written into ELR_EL1)

        ThreadState state { ThreadState::Runnable };
    };

    Thread* create_thread(ThreadEntry entry, void* arg, usize stack_size = 64 * 1024);
    void    destroy_thread(Thread* t);

    extern "C" void thread_exit();
}
