#include "kernel/mm/heap/kheap.hpp"
#include "kernel/mm/phys/page_alloc.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

namespace
{
    static constexpr u32 MAGIC = 0xC0FFEE42;

    static constexpr usize ALIGN = 16;
    static constexpr usize HEAP_PAGES = 1024; // 4MB if PAGE_SIZE=4096
    static constexpr usize MIN_SPLIT = 32;

    static inline usize align_up(usize x, usize a)
    {
        return (x + (a - 1)) & ~(a - 1);
    }

    struct Header
    {
        u32 magic;
        u32 flags; // bit0 = free, bit1 = large
        usize size; // payload size in bytes
        usize pages; // only for large allocations
        Header* next; // free list only
        Header* prev; // free list only
    };

    static Header* g_free_head = nullptr;

    static u8* g_heap_base = nullptr;
    static u8* g_heap_end  = nullptr;

    static usize g_total_bytes = 0;
    static usize g_free_bytes  = 0;

    static inline bool is_free(Header* h)
    {
        return (h->flags & 1u) != 0;
    }

    static inline void set_free(Header* h, bool v)
    {
        if (v)
        {
            h->flags |= 1u;
        }
        else
        {
            h->flags &= ~1u;
        }
    }

    static inline bool is_large(Header* h)
    {
        return (h->flags & 2u) != 0;
    }

    static inline void set_large(Header* h, bool v)
    {
        if (v)
        {
            h->flags |= 2u;
        }
        else
        {
            h->flags &= ~2u;
        }
    }

    static inline u8* payload(Header* h)
    {
        return (u8*)h + align_up(sizeof(Header), ALIGN);
    }

    static inline Header* header_from_payload(void* p)
    {
        u8* b = (u8*)p;
        return (Header*)(b - align_up(sizeof(Header), ALIGN));
    }

    static void remove_free(Header* h)
    {
        if (h->prev != nullptr)
        {
            h->prev->next = h->next;
        }
        else
        {
            g_free_head = h->next;
        }

        if (h->next != nullptr)
        {
            h->next->prev = h->prev;
        }

        h->next = nullptr;
        h->prev = nullptr;
    }

    static void insert_free_front(Header* h)
    {
        h->prev = nullptr;
        h->next = g_free_head;

        if (g_free_head != nullptr)
        {
            g_free_head->prev = h;
        }

        g_free_head = h;
    }

    static Header* next_phys(Header* h)
    {
        u8* n = payload(h) + h->size;

        if (n >= g_heap_end)
        {
            return nullptr;
        }

        return (Header*)n;
    }

    static Header* prev_phys(Header* h)
    {
        // We don't store footers, so we can't find prev in O(1).
        // We’ll do a linear scan from heap base (fine for milestone 8).
        Header* cur = (Header*)g_heap_base;
        Header* prev = nullptr;

        while ((u8*)cur < g_heap_end)
        {
            if (cur == h)
            {
                return prev;
            }

            prev = cur;

            u8* next = payload(cur) + cur->size;
            if (next <= (u8*)cur || next > g_heap_end)
            {
                break;
            }

            cur = (Header*)next;
        }

        return nullptr;
    }

    static void try_coalesce(Header* h)
    {
        Header* n = next_phys(h);

        if (n != nullptr && n->magic == MAGIC && is_free(n) && !is_large(n))
        {
            remove_free(n);

            usize overhead = (usize)((u8*)payload(n) - (u8*)n);
            h->size += overhead + n->size;
            g_free_bytes += overhead;

            n->magic = 0;
        }

        Header* p = prev_phys(h);

        if (p != nullptr && p->magic == MAGIC && is_free(p) && !is_large(p))
        {
            remove_free(h);

            usize overhead = (usize)((u8*)payload(h) - (u8*)h);
            p->size += overhead + h->size;
            g_free_bytes += overhead;

            h->magic = 0;
        }
    }

    static void split_if_possible(Header* h, usize want)
    {
        usize overhead = align_up(sizeof(Header), ALIGN);

        if (h->size < want + overhead + MIN_SPLIT)
        {
            return;
        }

        u8* new_hdr_addr = payload(h) + want;
        Header* nh = (Header*)new_hdr_addr;

        nh->magic = MAGIC;
        nh->flags = 0;
        set_free(nh, true);
        set_large(nh, false);

        nh->pages = 0;
        nh->size = h->size - want - overhead;
        nh->next = nullptr;
        nh->prev = nullptr;

        h->size = want;

        insert_free_front(nh);

        g_free_bytes -= overhead;
    }
}

namespace kheap
{
    void init()
    {
        void* base = phys::alloc_pages(HEAP_PAGES);

        if (base == nullptr)
        {
            panic("kheap::init: could not allocate heap arena");
        }

        g_heap_base = (u8*)base;
        g_heap_end  = g_heap_base + HEAP_PAGES * phys::PAGE_SIZE;

        Header* h = (Header*)g_heap_base;

        h->magic = MAGIC;
        h->flags = 0;
        set_free(h, true);
        set_large(h, false);

        h->pages = 0;
        h->next = nullptr;
        h->prev = nullptr;

        usize overhead = align_up(sizeof(Header), ALIGN);
        h->size = (usize)(g_heap_end - payload(h));

        g_free_head = h;

        g_total_bytes = h->size;
        g_free_bytes  = h->size;

        kprint::puts("kheap::init\n");
        kprint::puts("  base=");
        kprint::hex_u64((u64)(uintptr_t)g_heap_base);
        kprint::puts(" end=");
        kprint::hex_u64((u64)(uintptr_t)g_heap_end);
        kprint::puts("\n  total=");
        kprint::dec_u64(g_total_bytes);
        kprint::puts(" free=");
        kprint::dec_u64(g_free_bytes);
        kprint::puts("\n");
    }

    void* kmalloc(usize size)
    {
        if (size == 0)
        {
            return nullptr;
        }

        size = align_up(size, ALIGN);

        Header* cur = g_free_head;

        while (cur != nullptr)
        {
            if (cur->magic != MAGIC)
            {
                panic("kmalloc: corrupted free list");
            }

            if (is_free(cur) && !is_large(cur) && cur->size >= size)
            {
                remove_free(cur);
                set_free(cur, false);

                g_free_bytes -= size;

                split_if_possible(cur, size);

                return (void*)payload(cur);
            }

            cur = cur->next;
        }

        // Fallback: allocate a dedicated contiguous run for "large" requests
        usize overhead = align_up(sizeof(Header), ALIGN);
        usize total = size + overhead;
        usize pages = align_up(total, phys::PAGE_SIZE) / phys::PAGE_SIZE;

        void* base = phys::alloc_pages(pages);
        if (base == nullptr)
        {
            return nullptr;
        }

        Header* h = (Header*)base;
        h->magic = MAGIC;
        h->flags = 0;
        set_free(h, false);
        set_large(h, true);

        h->size = size;
        h->pages = pages;
        h->next = nullptr;
        h->prev = nullptr;

        return (void*)payload(h);
    }

    void kfree(void* ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        Header* h = header_from_payload(ptr);

        if (h->magic != MAGIC)
        {
            panic("kfree: bad pointer or corrupted header");
        }

        if (is_large(h))
        {
            phys::free_pages((void*)h, h->pages);
            return;
        }

        if (is_free(h))
        {
            panic("kfree: double free");
        }

        set_free(h, true);
        insert_free_front(h);

        g_free_bytes += h->size;

        try_coalesce(h);
    }

    void stats()
    {
        kprint::puts("kheap stats: total=");
        kprint::dec_u64(g_total_bytes);
        kprint::puts(" free=");
        kprint::dec_u64(g_free_bytes);
        kprint::puts("\n");
    }
}
