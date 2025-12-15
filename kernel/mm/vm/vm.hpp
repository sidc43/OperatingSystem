#pragma once
#include "types.hpp"

namespace vm
{
    enum Flags : u64
    {
        READWRITE = 1ull << 0,
        DEVICE    = 1ull << 1,
        NOEXEC    = 1ull << 2,
        USER      = 1ull << 3
    };

    void init();

    bool map_page(u64 va, u64 pa, u64 flags);
    bool unmap_page(u64 va);

    void switch_to_kernel_table();
    bool map_range(u64 va, u64 pa, u64 size, u64 flags);
}
