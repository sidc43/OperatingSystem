#include "kernel/syscall/syscall.hpp"

#include "kernel/core/print.hpp"
#include "kernel/usermode/usersched.hpp"

extern "C" u64 g_el0_return_pc;
extern "C" u64 g_el0_result;

namespace
{
    static inline u64& reg(void* frame, usize idx)
    {
        return ((u64*)frame)[idx];
    }

    static inline u64 esr_ec(u64 esr)
    {
        return (esr >> 26) & 0x3Fu;
    }

    static inline void set_elr(u64 v)
    {
        asm volatile("msr ELR_EL1, %0" :: "r"(v) : "memory");
        asm volatile("isb" ::: "memory");
    }

    static inline void set_spsr(u64 v)
    {
        asm volatile("msr SPSR_EL1, %0" :: "r"(v) : "memory");
        asm volatile("isb" ::: "memory");
    }

    static constexpr u64 SYS_write = 64;
    static constexpr u64 SYS_exit  = 93;
    static constexpr u64 SYS_yield = 124;
}

namespace syscall
{
    void* handle_svc(u64 esr, u64 elr, void* frame)
    {
        // EC=0x15 => SVC64
        if (esr_ec(esr) != 0x15)
        {
            return frame;
        }

        u64 sysno = reg(frame, 8);
        u64 a0 = reg(frame, 0);
        u64 a1 = reg(frame, 1);
        u64 a2 = reg(frame, 2);

        if (sysno == SYS_write)
        {
            const char* p = (const char*)(uintptr_t)a1;
            u64 n = a2;

            for (u64 i = 0; i < n; i++)
            {
                kprint::putc(p[i]);
            }

            reg(frame, 0) = n;
            set_elr(elr + 4);
            return frame;
        }

        if (sysno == SYS_yield)
        {
            reg(frame, 0) = 0;

            if (usersched::active())
            {
                return usersched::on_yield(frame, elr);
            }

            set_elr(elr + 4);
            return frame;
        }

        if (sysno == SYS_exit)
        {
            if (usersched::active())
            {
                return usersched::on_exit(frame, a0);
            }

            g_el0_result = a0;

            kprint::puts("\n[sys_exit] code=");
            kprint::dec_u64(g_el0_result);
            kprint::puts("\n");

            set_spsr(5);                 // EL1h
            set_elr(g_el0_return_pc);     // jump back to kernel
            return frame;
        }

        reg(frame, 0) = (u64)-1;
        set_elr(elr + 4);
        return frame;
    }
}
