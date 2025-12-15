#pragma once
#include "types.hpp"

namespace kheap
{
    void init();

    void* kmalloc(usize size);
    void  kfree(void* ptr);

    void  stats();
}
