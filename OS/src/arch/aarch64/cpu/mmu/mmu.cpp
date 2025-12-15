#include "src/arch/aarch64/cpu/mmu/mmu.hpp"
#include "types.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

namespace
{
    constexpr u64 L1_ENTRIES = 512;
    constexpr u64 TABLE_ALIGN = 4096;

    alignas(TABLE_ALIGN) static u64 l1_table[L1_ENTRIES];

    constexpr u64 MAIR_ATTR_NORMAL = 0xFF;
    constexpr u64 MAIR_ATTR_DEVICE = 0x04;

    constexpr u64 DESC_BLOCK = 0b01;

    constexpr u64 AF = (1ull << 10);

    constexpr u64 SH_NONE  = (0ull << 8);
    constexpr u64 SH_INNER = (3ull << 8);

    constexpr u64 AP_EL1_RW = (0ull << 6);

    constexpr u64 ATTRINDX(u64 i)
    {
        return (i & 0x7) << 2;
    }

    constexpr u64 PXN = (1ull << 53);
    constexpr u64 UXN = (1ull << 54);

    inline void dsb_ish()
    {
        asm volatile("dsb ish" ::: "memory");
    }

    inline void isb()
    {
        asm volatile("isb" ::: "memory");
    }

    inline void tlbi_vmalle1()
    {
        asm volatile("tlbi vmalle1" ::: "memory");
    }

    inline void write_mair(u64 v)
    {
        asm volatile("msr MAIR_EL1, %0" :: "r"(v));
    }

    inline void write_tcr(u64 v)
    {
        asm volatile("msr TCR_EL1, %0" :: "r"(v));
    }

    inline void write_ttbr0(u64 v)
    {
        asm volatile("msr TTBR0_EL1, %0" :: "r"(v));
    }

    inline u64 read_sctlr()
    {
        u64 v;
        asm volatile("mrs %0, SCTLR_EL1" : "=r"(v));
        return v;
    }

    inline void write_sctlr(u64 v)
    {
        asm volatile("msr SCTLR_EL1, %0" :: "r"(v));
    }

    inline u64 block_desc(u64 pa_base, u64 attr, u64 sh, u64 xn_bits)
    {
        return (pa_base & ~((1ull << 30) - 1)) | DESC_BLOCK | attr | sh | AP_EL1_RW | AF | xn_bits;
    }
}

namespace mmu
{
    bool enabled()
    {
        return (read_sctlr() & 1ull) != 0;
    }

    void init()
    {
        if (enabled())
        {
            kprint::puts("mmu::init: already enabled\n");
            return;
        }

        for (u64 i = 0; i < L1_ENTRIES; i++)
        {
            l1_table[i] = 0;
        }

        l1_table[0] = block_desc(0x00000000ull, ATTRINDX(1), SH_NONE, PXN | UXN);
        l1_table[1] = block_desc(0x40000000ull, ATTRINDX(0), SH_INNER, UXN);

        u64 mair =
            (MAIR_ATTR_NORMAL << (0 * 8)) |
            (MAIR_ATTR_DEVICE << (1 * 8));

        write_mair(mair);

        u64 tcr = 0;

        tcr |= (25ull << 0);

        tcr |= (0ull << 14);

        tcr |= (3ull << 12);
        tcr |= (1ull << 10);
        tcr |= (1ull << 8);

        tcr |= (0ull << 7);

        tcr |= (1ull << 23);

        tcr |= (0ull << 32);

        write_tcr(tcr);

        write_ttbr0((u64)(uintptr_t)l1_table);

        dsb_ish();
        tlbi_vmalle1();
        dsb_ish();
        isb();

        u64 sctlr = read_sctlr();

        sctlr |= (1ull << 0);
        sctlr |= (1ull << 2);
        sctlr |= (1ull << 12);

        write_sctlr(sctlr);
        isb();

        if (!enabled())
        {
            panic("mmu did not enable");
        }

        kprint::puts("mmu::init: enabled\n");
    }
}
