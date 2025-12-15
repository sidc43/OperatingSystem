#include "types.hpp"
#include "kernel/mm/heap/kheap.hpp"
#include "kernel/core/panic.hpp"

void* operator new(usize size)
{
    void* p = kheap::kmalloc(size);

    if (p == nullptr)
    {
        panic("operator new: out of memory");
    }

    return p;
}

void* operator new[](usize size)
{
    void* p = kheap::kmalloc(size);

    if (p == nullptr)
    {
        panic("operator new[]: out of memory");
    }

    return p;
}

void operator delete(void* ptr) noexcept
{
    kheap::kfree(ptr);
}

void operator delete[](void* ptr) noexcept
{
    kheap::kfree(ptr);
}

void operator delete(void* ptr, usize) noexcept
{
    kheap::kfree(ptr);
}

void operator delete[](void* ptr, usize) noexcept
{
    kheap::kfree(ptr);
}
