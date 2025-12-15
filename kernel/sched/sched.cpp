#include "kernel/sched/sched.hpp"
#include "kernel/core/panic.hpp"

static inline void irq_disable()
{
    asm volatile("msr daifset, #2" ::: "memory");
}

static inline void irq_enable()
{
    asm volatile("msr daifclr, #2" ::: "memory");
}

static inline void set_elr_el1(u64 v)
{
    asm volatile("msr elr_el1, %0" :: "r"(v) : "memory");
}

namespace sched
{
    static constexpr usize MAX_THREADS = 32;

    static Thread* g_threads[MAX_THREADS] {};
    static usize   g_count = 0;

    static Thread* g_current = nullptr;
    static usize   g_index = 0;

    static Context g_boot_ctx {};

    void init()
    {
        g_count = 0;
        g_current = nullptr;
        g_index = 0;

        for (usize i = 0; i < MAX_THREADS; i++)
        {
            g_threads[i] = nullptr;
        }
    }

    void add(Thread* t)
    {
        if (!t)
        {
            return;
        }

        if (g_count >= MAX_THREADS)
        {
            panic("sched: too many threads");
        }

        g_threads[g_count++] = t;
    }

    static Thread* pick_next()
    {
        if (g_count == 0)
        {
            return nullptr;
        }

        for (usize tries = 0; tries < g_count; tries++)
        {
            g_index = (g_index + 1) % g_count;
            Thread* t = g_threads[g_index];
            if (t && t->state == ThreadState::Runnable)
            {
                return t;
            }
        }

        return nullptr;
    }

    void yield()
    {
        irq_disable();

        if (!g_current)
        {
            irq_enable();
            return;
        }

        Thread* next = pick_next();
        if (!next || next == g_current)
        {
            irq_enable();
            return;
        }

        Thread* prev = g_current;
        g_current = next;

        irq_enable();
        context_switch(&prev->ctx, &next->ctx);
    }

    void start()
    {
        if (g_count == 0)
        {
            panic("sched: no threads to run");
        }

        g_current = nullptr;
        g_index = 0;

        Thread* first = nullptr;
        for (usize i = 0; i < g_count; i++)
        {
            Thread* t = g_threads[i];
            if (t && t->state == ThreadState::Runnable)
            {
                first = t;
                g_index = i;
                break;
            }
        }

        if (!first)
        {
            panic("sched: no runnable threads");
        }

        g_current = first;

        // Let IRQs preempt once we’re in threads
        irq_enable();

        // Bootstrap into first thread (U1 style). After the first timer IRQ,
        // we can switch threads by returning different trapframes.
        context_switch(&g_boot_ctx, &first->ctx);

        panic("sched: returned to boot context");
    }

    void* on_irq(void* frame, u64 elr)
    {
        // If scheduler not started yet, just return same frame.
        if (!g_current)
        {
            return frame;
        }

        // Save interrupted state into current thread
        g_current->saved_frame = frame;
        g_current->saved_elr   = elr;

        Thread* next = pick_next();
        if (!next || next == g_current)
        {
            return frame;
        }

        g_current = next;

        if (!next->saved_frame || next->saved_elr == 0)
        {
            panic("sched: next thread has no saved frame/elr");
        }

        // Set return PC for exception return
        set_elr_el1(next->saved_elr);

        // Return the trapframe pointer that vectors.S will restore
        return next->saved_frame;
    }

    void on_thread_exit()
    {
        irq_disable();

        if (!g_current)
        {
            panic("sched: thread_exit with no current");
        }

        g_current->state = ThreadState::Exited;

        Thread* next = pick_next();
        if (!next)
        {
            panic("sched: no runnable threads left");
        }

        Thread* prev = g_current;
        g_current = next;

        irq_enable();
        context_switch(&prev->ctx, &next->ctx);

        panic("sched: on_thread_exit returned");
    }

    extern "C" void thread_exit()
    {
        on_thread_exit();
    }
}
