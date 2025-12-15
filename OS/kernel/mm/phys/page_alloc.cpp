#include "page_alloc.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

extern "C"
{
    extern u8 __kernel_end;
}

namespace
{
    static constexpr u64 RAM_BASE = 0x40000000;

    static constexpr u64 RAM_SIZE = 512ull * 1024ull * 1024ull;

    static constexpr usize MAX_PAGES = (usize)(RAM_SIZE / phys::PAGE_SIZE);
    static constexpr usize BITMAP_WORD_BITS = 64;
    static constexpr usize BITMAP_WORDS = (MAX_PAGES + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

    static u64 g_bitmap[BITMAP_WORDS];
    static usize g_free_pages = 0;

    static inline u64 align_up(u64 x, u64 a)
    {
        return (x + (a - 1)) & ~(a - 1);
    }

    static inline bool bit_test(usize i)
    {
        usize w = i / BITMAP_WORD_BITS;
        usize b = i % BITMAP_WORD_BITS;
        return (g_bitmap[w] >> b) & 1ull;
    }

    static inline void bit_set(usize i)
    {
        usize w = i / BITMAP_WORD_BITS;
        usize b = i % BITMAP_WORD_BITS;
        g_bitmap[w] |= (1ull << b);
    }

    static inline void bit_clear(usize i)
    {
        usize w = i / BITMAP_WORD_BITS;
        usize b = i % BITMAP_WORD_BITS;
        g_bitmap[w] &= ~(1ull << b);
    }

    static inline usize addr_to_page(u64 addr)
    {
        return (usize)((addr - RAM_BASE) / phys::PAGE_SIZE);
    }

    static inline u64 page_to_addr(usize page)
    {
        return RAM_BASE + (u64)page * phys::PAGE_SIZE;
    }

    static void mark_used_range(u64 start, u64 end)
    {
        if (end <= start)
        {
            return;
        }

        if (start < RAM_BASE)
        {
            start = RAM_BASE;
        }

        u64 ram_end = RAM_BASE + RAM_SIZE;
        if (end > ram_end)
        {
            end = ram_end;
        }

        start = align_up(start, phys::PAGE_SIZE);
        end = align_up(end, phys::PAGE_SIZE);

        for (u64 a = start; a < end; a += phys::PAGE_SIZE)
        {
            usize p = addr_to_page(a);
            if (p < MAX_PAGES && !bit_test(p))
            {
                bit_set(p);
                if (g_free_pages > 0)
                {
                    g_free_pages--;
                }
            }
        }
    }
}

namespace phys
{
    void init()
    {
        for (usize i = 0; i < BITMAP_WORDS; i++)
        {
            g_bitmap[i] = 0;
        }

        g_free_pages = MAX_PAGES;

        u64 kernel_end = (u64)(uintptr_t)&__kernel_end;
        u64 free_start = align_up(kernel_end, PAGE_SIZE);
        u64 free_end = RAM_BASE + RAM_SIZE;

        mark_used_range(RAM_BASE, free_start);

        kprint::puts("phys::init\n");
        kprint::puts("  RAM_BASE: ");
        kprint::hex_u64(RAM_BASE);
        kprint::puts("\n  RAM_END : ");
        kprint::hex_u64(free_end);
        kprint::puts("\n  free from: ");
        kprint::hex_u64(free_start);
        kprint::puts("\n  free pages: ");
        kprint::dec_u64(g_free_pages);
        kprint::puts("\n");
    }

    void* alloc_page()
    {
        for (usize p = 0; p < MAX_PAGES; p++)
        {
            if (!bit_test(p))
            {
                bit_set(p);
                if (g_free_pages == 0)
                {
                    panic("phys allocator accounting bug");
                }
                g_free_pages--;

                u64 addr = page_to_addr(p);
                return (void*)(uintptr_t)addr;
            }
        }

        return nullptr;
    }

    void free_page(void* ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        u64 addr = (u64)(uintptr_t)ptr;

        if (addr < RAM_BASE || addr >= (RAM_BASE + RAM_SIZE))
        {
            panic("free_page: address out of RAM range");
        }

        if ((addr % PAGE_SIZE) != 0)
        {
            panic("free_page: address not page-aligned");
        }

        usize p = addr_to_page(addr);

        if (!bit_test(p))
        {
            panic("free_page: double free");
        }

        bit_clear(p);
        g_free_pages++;
    }

    usize free_pages()
    {
        return g_free_pages;
    }

    void* alloc_pages(usize count)
    {
        if (count == 0)
        {
            return nullptr;
        }

        usize run = 0;
        usize start = 0;

        for (usize p = 0; p < MAX_PAGES; p++)
        {
            if (!bit_test(p))
            {
                if (run == 0)
                {
                    start = p;
                }

                run++;

                if (run == count)
                {
                    for (usize i = 0; i < count; i++)
                    {
                        bit_set(start + i);
                    }

                    if (g_free_pages < count)
                    {
                        panic("phys allocator accounting bug");
                    }

                    g_free_pages -= count;

                    u64 addr = page_to_addr(start);
                    return (void*)(uintptr_t)addr;
                }
            }
            else
            {
                run = 0;
            }
        }

        return nullptr;
    }

    void free_pages(void* base, usize count)
    {
        if (base == nullptr || count == 0)
        {
            return;
        }

        u64 addr = (u64)(uintptr_t)base;

        if (addr < RAM_BASE || addr >= (RAM_BASE + RAM_SIZE))
        {
            panic("free_pages: address out of RAM range");
        }

        if ((addr % PAGE_SIZE) != 0)
        {
            panic("free_pages: address not page-aligned");
        }

        usize first = addr_to_page(addr);

        for (usize i = 0; i < count; i++)
        {
            usize p = first + i;

            if (p >= MAX_PAGES)
            {
                panic("free_pages: range out of bounds");
            }

            if (!bit_test(p))
            {
                panic("free_pages: double free in range");
            }

            bit_clear(p);
        }

        g_free_pages += count;
    }
}
