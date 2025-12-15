#pragma once
#include "types.hpp"

namespace phys
{
    static constexpr usize PAGE_SIZE = 4096;

    void init();

    void* alloc_page();
    void free_page(void* p);

    usize free_pages();

    void* alloc_pages(usize count);
    void  free_pages(void* base, usize count);
}
