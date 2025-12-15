#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/phys/page_alloc.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

namespace
{
    static constexpr u64 RAM_BASE = 0x40000000ull;
    static constexpr u64 RAM_SIZE = 512ull * 1024ull * 1024ull;

    static constexpr u64 DEVICE_BASE = 0x00000000ull;
    static constexpr u64 DEVICE_SIZE = 0x40000000ull;

    static constexpr u64 PAGE_SIZE = 4096;
    static constexpr u64 TABLE_ENTRIES = 512;

    static constexpr u64 DESC_INVALID = 0;
    static constexpr u64 DESC_TABLE   = 0b11;
    static constexpr u64 DESC_PAGE    = 0b11;

    static constexpr u64 AF = (1ull << 10);
    static constexpr u64 SH_NONE  = (0ull << 8);
    static constexpr u64 SH_INNER = (3ull << 8);

    static constexpr u64 AP_EL1_RW = (0ull << 6);
    static constexpr u64 AP_EL0_RW = (1ull << 6);

    static constexpr u64 PXN = (1ull << 53);
    static constexpr u64 UXN = (1ull << 54);

    static constexpr u64 ATTRINDX(u64 i)
    {
        return (i & 0x7) << 2;
    }

    static constexpr u64 MAIR_ATTR_NORMAL = 0xFF;
    static constexpr u64 MAIR_ATTR_DEVICE = 0x04;

    static u64* g_l1 = nullptr;

    static inline void zero_page(void* p)
    {
        volatile u64* w = (volatile u64*)p;
        for (usize i = 0; i < (PAGE_SIZE / 8); i++)
        {
            w[i] = 0;
        }
    }

    static inline u64 idx_l1(u64 va)
    {
        return (va >> 30) & 0x1FF;
    }

    static inline u64 idx_l2(u64 va)
    {
        return (va >> 21) & 0x1FF;
    }

    static inline u64 idx_l3(u64 va)
    {
        return (va >> 12) & 0x1FF;
    }

    static inline u64 table_pa_from_desc(u64 desc)
    {
        return desc & 0x0000FFFFFFFFF000ull;
    }

    static inline bool is_table_desc(u64 desc)
    {
        return (desc & 0b11) == DESC_TABLE;
    }

    static inline u64 make_table_desc(u64 table_pa)
    {
        return (table_pa & 0x0000FFFFFFFFF000ull) | DESC_TABLE;
    }

    static inline u64 make_page_desc(u64 pa, u64 flags)
    {
        bool device = (flags & vm::DEVICE) != 0;
        bool noexec = (flags & vm::NOEXEC) != 0;
        bool user   = (flags & vm::USER) != 0;

        u64 attr = device ? ATTRINDX(1) : ATTRINDX(0);
        u64 sh   = device ? SH_NONE : SH_INNER;

        u64 ap = user ? AP_EL0_RW : AP_EL1_RW;

        u64 xn = 0;

        if (user)
        {
            // User mappings:
            // - prevent EL1 execution from user pages
            xn |= PXN;

            // - allow EL0 execute only if not NOEXEC
            if (noexec)
            {
                xn |= UXN;
            }
        }
        else
        {
            // Kernel mappings:
            // - never allow EL0 to execute kernel pages
            xn |= UXN;

            // - block EL1 execute for device/noexec
            if (device || noexec)
            {
                xn |= PXN;
            }
        }

        return (pa & 0x0000FFFFFFFFF000ull) |
               DESC_PAGE |
               attr |
               sh |
               ap |
               AF |
               xn;
    }

    static u64* ensure_table(u64* parent, u64 index)
    {
        u64 e = parent[index];

        if (e == DESC_INVALID)
        {
            void* page = phys::alloc_page();
            if (page == nullptr)
            {
                return nullptr;
            }

            zero_page(page);
            parent[index] = make_table_desc((u64)(uintptr_t)page);
            return (u64*)page;
        }

        if (!is_table_desc(e))
        {
            return nullptr;
        }

        return (u64*)(uintptr_t)table_pa_from_desc(e);
    }

    static inline u64 read_parange()
    {
        u64 mmfr0;
        asm volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r"(mmfr0));
        return mmfr0 & 0xFull;
    }

    static void mmu_set_mair_tcr_ttbr0(u64 ttbr0)
    {
        u64 mair =
            (MAIR_ATTR_NORMAL << (0 * 8)) |
            (MAIR_ATTR_DEVICE << (1 * 8));

        asm volatile("msr MAIR_EL1, %0" :: "r"(mair));

        u64 tcr = 0;

        tcr |= (25ull << 0);   

        tcr |= (0ull  << 14);  
        tcr |= (3ull  << 12);  
        tcr |= (1ull  << 10);  
        tcr |= (1ull  << 8);   

        tcr |= (0ull  << 7);   
        tcr |= (1ull  << 23);  

        u64 parange = read_parange();
        u64 ips = (parange > 6) ? 6 : parange;
        tcr |= (ips & 0x7ull) << 32;

        asm volatile("msr TCR_EL1, %0" :: "r"(tcr));
        asm volatile("msr TTBR0_EL1, %0" :: "r"(ttbr0));

        asm volatile("dsb ish" ::: "memory");
        asm volatile("tlbi vmalle1" ::: "memory");
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");
    }

    static void mmu_enable_if_needed()
    {
        u64 sctlr;
        asm volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));

        if ((sctlr & 1ull) == 0)
        {
            sctlr |= (1ull << 0);
            sctlr |= (1ull << 2);
            sctlr |= (1ull << 12);
            asm volatile("msr SCTLR_EL1, %0" :: "r"(sctlr));
            asm volatile("isb" ::: "memory");
        }
    }

    static void identity_map_range(u64 base, u64 size, u64 flags)
    {
        u64 start = base & ~(PAGE_SIZE - 1);
        u64 end   = (base + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (u64 va = start; va < end; va += PAGE_SIZE)
        {
            if (!vm::map_page(va, va, flags))
            {
                panic("vm identity_map_range failed");
            }
        }
    }
}

namespace vm
{
    void init()
    {
        void* root = phys::alloc_page();
        if (root == nullptr)
        {
            panic("vm::init: no memory for L1");
        }

        zero_page(root);
        g_l1 = (u64*)root;

        identity_map_range(DEVICE_BASE, DEVICE_SIZE, DEVICE | NOEXEC);
        identity_map_range(RAM_BASE, RAM_SIZE, READWRITE);

        kprint::puts("vm::init: built kernel page tables\n");
    }

    void switch_to_kernel_table()
    {
        if (g_l1 == nullptr)
        {
            panic("vm::switch_to_kernel_table: vm not initialized");
        }

        mmu_set_mair_tcr_ttbr0((u64)(uintptr_t)g_l1);
        mmu_enable_if_needed();

        kprint::puts("vm: switched TTBR0 to kernel page tables\n");
    }

    bool map_page(u64 va, u64 pa, u64 flags)
    {
        if ((va & (PAGE_SIZE - 1)) != 0 || (pa & (PAGE_SIZE - 1)) != 0)
        {
            return false;
        }

        if (g_l1 == nullptr)
        {
            return false;
        }

        u64* l2 = ensure_table(g_l1, idx_l1(va));
        if (l2 == nullptr)
        {
            return false;
        }

        u64* l3 = ensure_table(l2, idx_l2(va));
        if (l3 == nullptr)
        {
            return false;
        }

        u64 i3 = idx_l3(va);

        if (l3[i3] != DESC_INVALID)
        {
            return false;
        }

        l3[i3] = make_page_desc(pa, flags);

        asm volatile("dsb ish" ::: "memory");
        asm volatile("tlbi vae1, %0" :: "r"(va >> 12));
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");

        return true;
    }

    bool unmap_page(u64 va)
    {
        if ((va & (PAGE_SIZE - 1)) != 0)
        {
            return false;
        }

        if (g_l1 == nullptr)
        {
            return false;
        }

        u64 e1 = g_l1[idx_l1(va)];
        if (!is_table_desc(e1))
        {
            return false;
        }

        u64* l2 = (u64*)(uintptr_t)table_pa_from_desc(e1);

        u64 e2 = l2[idx_l2(va)];
        if (!is_table_desc(e2))
        {
            return false;
        }

        u64* l3 = (u64*)(uintptr_t)table_pa_from_desc(e2);

        u64 i3 = idx_l3(va);

        if (l3[i3] == DESC_INVALID)
        {
            return false;
        }

        l3[i3] = DESC_INVALID;

        asm volatile("dsb ish" ::: "memory");
        asm volatile("tlbi vae1, %0" :: "r"(va >> 12));
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");

        return true;
    }

    bool map_range(u64 va, u64 pa, u64 size, u64 flags)
    {
        if ((va & (PAGE_SIZE - 1)) != 0 || (pa & (PAGE_SIZE - 1)) != 0)
        {
            return false;
        }

        u64 start = va;
        u64 end   = va + ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

        u64 cur_va = start;
        u64 cur_pa = pa;

        while (cur_va < end)
        {
            if (!map_page(cur_va, cur_pa, flags))
            {
                return false;
            }

            cur_va += PAGE_SIZE;
            cur_pa += PAGE_SIZE;
        }

        return true;
    }
}
