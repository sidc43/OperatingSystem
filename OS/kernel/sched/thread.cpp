#include "kernel/sched/thread.hpp"
#include "kernel/mm/heap/kheap.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/sched/context.hpp"

namespace sched
{
    static inline u64 align_down(u64 v, u64 a)
    {
        return v & ~(a - 1);
    }

    static inline void memzero(void* p, usize n)
    {
        u8* b = (u8*)p;
        for (usize i = 0; i < n; i++)
        {
            b[i] = 0;
        }
    }

    Thread* create_thread(ThreadEntry entry, void* arg, usize stack_size)
    {
        Thread* t = (Thread*)kheap::kmalloc(sizeof(Thread));
        if (!t)
        {
            panic("sched: kmalloc thread failed");
        }
        memzero(t, sizeof(Thread));

        void* stack = kheap::kmalloc(stack_size);
        if (!stack)
        {
            panic("sched: kmalloc stack failed");
        }

        t->stack_base = stack;
        t->stack_size = stack_size;
        t->state = ThreadState::Runnable;

        // 16-byte aligned stack top
        u64 sp_top = (u64)(uintptr_t)stack + (u64)stack_size;
        sp_top = align_down(sp_top, 16);

        // ---- U1 bootstrap context_switch path ----
        t->ctx.x19 = (u64)(uintptr_t)entry;
        t->ctx.x20 = (u64)(uintptr_t)arg;
        t->ctx.x30 = (u64)(uintptr_t)&thread_trampoline;
        t->ctx.sp  = sp_top;

        // ---- U2 preemption path: synthesize an initial interrupt frame ----
        // We want: when scheduled via interrupt-return, restore regs from a frame,
        // pop it, and eret to ELR_EL1. The frame lives at (sp_top - FULL_FRAME_SIZE).
        u64 full_frame_base = sp_top - arch::FULL_FRAME_SIZE;

        // The "main frame" begins at full_frame_base (first 272 bytes).
        arch::TrapFrame* tf = (arch::TrapFrame*)(uintptr_t)full_frame_base;
        memzero(tf, sizeof(arch::TrapFrame));

        // Pre-frame (x16/x17) sits above main frame: we can just leave as zero.

        // thread_trampoline expects x19=entry, x20=arg.
        tf->x18 = 0;
        tf->x19 = (u64)(uintptr_t)entry;
        tf->x20 = (u64)(uintptr_t)arg;

        // Resume PC for this thread
        t->saved_elr = (u64)(uintptr_t)&thread_trampoline;

        // Saved frame pointer is base of main frame (272 bytes)
        t->saved_frame = (void*)(uintptr_t)full_frame_base;

        return t;
    }

    void destroy_thread(Thread* t)
    {
        if (!t)
        {
            return;
        }

        if (t->stack_base)
        {
            kheap::kfree(t->stack_base);
            t->stack_base = nullptr;
        }

        kheap::kfree(t);
    }
}
