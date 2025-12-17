#include "kernel/tests/usermode_tests.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/phys/page_alloc.hpp"

extern "C" void enter_el0(u64 user_sp, u64 user_pc, u64 arg0);

extern "C" u8 el0_blob_start[];
extern "C" u8 el0_blob_end[];

extern "C" u64 g_el0_return_pc = 0;
extern "C" u64 g_el0_result    = 0;

namespace
{
    static constexpr u64 PAGE_SIZE = 4096;

    static constexpr u64 USER_BASE     = 0x0000000100000000ull;
    static constexpr u64 USER_CODE_VA  = USER_BASE + 0x0000ull;
    static constexpr u64 USER_STACK_VA = USER_BASE + 0x10000ull;

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
}

namespace tests
{
    void usermode_smoke_test()
    {
        kprint::puts("\n=== usermode smoke test (EL0) ===\n");

        void* code_page  = phys::alloc_page();
        void* stack_page = phys::alloc_page();

        if (!code_page || !stack_page)
        {
            panic("usermode: phys alloc failed");
        }

        u64 code_pa  = (u64)(uintptr_t)code_page;
        u64 stack_pa = (u64)(uintptr_t)stack_page;

        bool ok = true;

        ok = vm::map_page(USER_CODE_VA, code_pa, vm::USER);
        if (!ok)
        {
            panic("usermode: map user code failed");
        }

        ok = vm::map_page(USER_STACK_VA, stack_pa, vm::USER | vm::READWRITE | vm::NOEXEC);
        if (!ok)
        {
            panic("usermode: map user stack failed");
        }

        u64 blob_size = (u64)(uintptr_t)(el0_blob_end - el0_blob_start);
        if (blob_size > PAGE_SIZE)
        {
            panic("usermode: el0 blob too big");
        }

        memcpy8((u8*)code_page, el0_blob_start, blob_size);
        icache_sync(code_page, blob_size);

        u64 user_sp = USER_STACK_VA + PAGE_SIZE;
        user_sp = align_down(user_sp, 16);

        g_el0_result = 0;

        kprint::puts("entering EL0...\n");

        enter_el0(user_sp, USER_CODE_VA, 41);

        u64 el;
        asm volatile("mrs %0, CurrentEL" : "=r"(el));
        el = (el >> 2) & 3;

        kprint::puts("\n[EL0 SMOKE] returned back into C!\n");
        kprint::puts("CurrentEL=");
        kprint::dec_u64(el);
        kprint::puts("\nEL0 final x0=");
        kprint::dec_u64(g_el0_result);
        kprint::puts("\n");
    }
}
