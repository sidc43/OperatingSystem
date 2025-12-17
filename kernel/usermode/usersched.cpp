#include "kernel/usermode/usersched.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/phys/page_alloc.hpp"

extern "C" void enter_el0(u64 user_sp, u64 user_pc, u64 arg0);

extern "C" u8 el0_yield_blob_start[];
extern "C" u8 el0_yield_blob_end[];

namespace
{
    static constexpr u64 PAGE_SIZE = 4096;

    static constexpr u64 P0_BASE = 0x0000000100000000ull;
    static constexpr u64 P1_BASE = 0x0000000200000000ull;

    struct Proc
    {
        bool alive { true };

        u64 base {};
        u64 code_va {};
        u64 stack_va {};

        void* code_page {};
        void* stack_page {};

        u64 sp {};
        u64 pc {};
        u64 arg0 {};
    };

    static Proc g_p[2] {};
    static int  g_cur = 0;
    static bool g_active = false;

    static inline u64 align_down(u64 v, u64 a)
    {
        return v & ~(a - 1);
    }

    static void memcpy8(u8* dst, const u8* src, u64 n)
    {
        for (u64 i = 0; i < n; i++)
        {
            dst[i] = src[i];
        }
    }

    static void icache_sync(void* start, u64 size)
    {
        u64 a = (u64)(uintptr_t)start;
        u64 end = a + size;

        for (u64 p = a & ~63ull; p < end; p += 64)
        {
            asm volatile("dc cvau, %0" :: "r"(p) : "memory");
        }
        asm volatile("dsb ish" ::: "memory");

        for (u64 p = a & ~63ull; p < end; p += 64)
        {
            asm volatile("ic ivau, %0" :: "r"(p) : "memory");
        }
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");
    }

    static inline void write_sp_el0(u64 v)
    {
        asm volatile("msr SP_EL0, %0" :: "r"(v) : "memory");
        asm volatile("isb" ::: "memory");
    }

    static inline void set_elr_el1(u64 v)
    {
        asm volatile("msr ELR_EL1, %0" :: "r"(v) : "memory");
        asm volatile("isb" ::: "memory");
    }

    static inline void set_spsr_el1(u64 v)
    {
        asm volatile("msr SPSR_EL1, %0" :: "r"(v) : "memory");
        asm volatile("isb" ::: "memory");
    }

    static int pick_next_alive(int cur)
    {
        for (int tries = 0; tries < 2; tries++)
        {
            cur = (cur + 1) % 2;
            if (g_p[cur].alive)
            {
                return cur;
            }
        }
        return -1;
    }

    static u64 initial_user_sp(const Proc& p)
    {
        u64 sp = p.stack_va + PAGE_SIZE;
        return align_down(sp, 16);
    }

    static void init_proc(Proc& p, u64 base, u64 ch)
    {
        p.base = base;
        p.code_va  = base + 0x0000ull;
        p.stack_va = base + 0x10000ull;

        p.code_page  = phys::alloc_page();
        p.stack_page = phys::alloc_page();

        if (!p.code_page || !p.stack_page)
        {
            panic("usersched: phys alloc failed");
        }

        u64 code_pa  = (u64)(uintptr_t)p.code_page;
        u64 stack_pa = (u64)(uintptr_t)p.stack_page;

        if (!vm::map_page(p.code_va, code_pa, vm::USER))
        {
            panic("usersched: map user code failed");
        }

        if (!vm::map_page(p.stack_va, stack_pa, vm::USER | vm::READWRITE | vm::NOEXEC))
        {
            panic("usersched: map user stack failed");
        }

        u64 blob_size = (u64)(uintptr_t)(el0_yield_blob_end - el0_yield_blob_start);
        if (blob_size > PAGE_SIZE)
        {
            panic("usersched: blob too big");
        }

        memcpy8((u8*)p.code_page, el0_yield_blob_start, blob_size);
        icache_sync(p.code_page, blob_size);

        p.sp = initial_user_sp(p);
        p.pc = p.code_va;
        p.arg0 = ch;
        p.alive = true;
    }

    extern "C" void usersched_resume()
    {
        Proc& p = g_p[g_cur];

        write_sp_el0(p.sp);
        enter_el0(p.sp, p.pc, p.arg0);

        panic("usersched_resume: enter_el0 returned");
    }

    static void save_current_from_svc_frame(void* /*frame*/, u64 /*svc_elr*/)
    {
        Proc& p = g_p[g_cur];

        // IMPORTANT: we are NOT preserving full EL0 register state yet.
        // So we restart at blob entry every time to avoid relying on saved regs.
        p.pc = p.code_va;
        p.sp = initial_user_sp(p);
        // p.arg0 stays as 'A'/'B'
    }

    static void bounce_to_resume()
    {
        set_spsr_el1(0x3C5);
        set_elr_el1((u64)(uintptr_t)&usersched_resume);
    }
}

namespace usersched
{
    void start_ab()
    {
        g_active = true;

        kprint::puts("\n=== usermode AB yield test ===\n");
        kprint::puts("entering EL0 tasks (expect ABABAB...)\n");

        init_proc(g_p[0], P0_BASE, (u64)'A');
        init_proc(g_p[1], P1_BASE, (u64)'B');

        g_cur = 0;
        usersched_resume();
    }

    bool active()
    {
        return g_active;
    }

    void* on_yield(void* frame, u64 elr)
    {
        if (!g_active)
        {
            return frame;
        }

        save_current_from_svc_frame(frame, elr);

        int next = pick_next_alive(g_cur);
        if (next < 0)
        {
            panic("usersched: no runnable procs");
        }

        g_cur = next;

        bounce_to_resume();
        return frame;
    }

    void* on_exit(void* frame, u64 code)
    {
        if (!g_active)
        {
            return frame;
        }

        g_p[g_cur].alive = false;

        kprint::puts("\n[usersched] proc exit code=");
        kprint::dec_u64(code);
        kprint::puts("\n");

        int next = pick_next_alive(g_cur);
        if (next < 0)
        {
            kprint::puts("[usersched] all procs exited.\n");
            while (1) { asm volatile("wfe"); }
        }

        g_cur = next;

        bounce_to_resume();
        return frame;
    }
}
