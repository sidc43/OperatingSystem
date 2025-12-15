#include "kernel/mm/vm/fault.hpp"
#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/phys/page_alloc.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

namespace
{
    static constexpr u64 PAGE_SIZE = 4096;

    static constexpr u64 DEMAND_BASE = 0x60000000ull;
    static constexpr u64 DEMAND_SIZE = 256ull * 1024ull * 1024ull;

    static inline u64 align_down(u64 x, u64 a)
    {
        return x & ~(a - 1);
    }

    static u64 esr_ec(u64 esr)
    {
        return (esr >> 26) & 0x3F;
    }

    static u64 esr_iss(u64 esr)
    {
        return esr & 0xFFFFFF;
    }

    static bool is_abort_ec(u64 ec)
    {
        return (ec == 0x20) || (ec == 0x21) || (ec == 0x24) || (ec == 0x25);
    }

    static bool is_translation_fault(u64 fsc)
    {
        return (fsc >= 0x04) && (fsc <= 0x07);
    }

    static bool in_demand_region(u64 addr)
    {
        return (addr >= DEMAND_BASE) && (addr < (DEMAND_BASE + DEMAND_SIZE));
    }

    static const char* ec_name(u64 ec)
    {
        switch (ec)
        {
            case 0x20: return "Instruction Abort (lower EL)";
            case 0x21: return "Instruction Abort (same EL)";
            case 0x24: return "Data Abort (lower EL)";
            case 0x25: return "Data Abort (same EL)";
        }
        return "Not an abort EC";
    }
}

namespace vm_fault
{
    bool try_handle(u64 vector_id, u64 esr, u64 elr, u64 far)
    {
        (void)vector_id;
        (void)elr;

        u64 ec = esr_ec(esr);

        if (!is_abort_ec(ec))
        {
            return false;
        }

        u64 iss = esr_iss(esr);
        u64 fsc = iss & 0x3F;

        if (!is_translation_fault(fsc))
        {
            return false;
        }

        if (!in_demand_region(far))
        {
            return false;
        }

        u64 va = align_down(far, PAGE_SIZE);

        void* page = phys::alloc_page();
        if (page == nullptr)
        {
            return false;
        }

        bool ok = vm::map_page(va, (u64)(uintptr_t)page, vm::READWRITE | vm::NOEXEC);
        if (!ok)
        {
            // If mapping failed, free the page so we don’t leak.
            phys::free_page(page);
            return false;
        }

        // Optional: print once per page allocation (can be noisy)
        kprint::puts("[demand] mapped page at VA=");
        kprint::hex_u64(va);
        kprint::puts(" PA=");
        kprint::hex_u64((u64)(uintptr_t)page);
        kprint::puts("\n");

        return true;
    }

    void dump_and_panic(u64 vector_id, u64 esr, u64 elr, u64 far)
    {
        u64 ec  = esr_ec(esr);
        u64 iss = esr_iss(esr);

        kprint::puts("\n\n=== PAGE FAULT ===\n");
        kprint::puts("vector_id: ");
        kprint::dec_u64(vector_id);
        kprint::puts("\n");

        kprint::puts("type: ");
        kprint::puts(ec_name(ec));
        kprint::puts("\n");

        kprint::puts("ESR: ");
        kprint::hex_u64(esr);
        kprint::puts("\nISS: ");
        kprint::hex_u64(iss);
        kprint::puts("\n");

        kprint::puts("FAR: ");
        kprint::hex_u64(far);
        kprint::puts("\n");

        kprint::puts("ELR: ");
        kprint::hex_u64(elr);
        kprint::puts("\n");

        panic("page fault");
    }
}
