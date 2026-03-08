/*
  heap.cpp - free-list heap allocator for the kernel
  manages a 16mib region defined by __heap_start/__heap_end in the linker script
  supports arbitrary power-of-two alignment, splitting on alloc, and coalescing on free
  every block has a magic value so we can catch corruption
*/
#include "kernel/mm/heap.hpp"
#include "kernel/core/panic.hpp"
#include <stdint.h>

extern "C" uint8_t __heap_start[];
extern "C" uint8_t __heap_end[];

namespace kheap {

static constexpr uint32_t MAGIC     = 0xB10CB10Cu;
static constexpr size_t   HDR_SIZE  = 32;
static constexpr size_t   MIN_SPLIT = HDR_SIZE + 16;

struct Block {
    uint32_t magic;
    uint32_t flags;
    size_t   size;
    Block*   next;
    Block*   prev;
};
static_assert(sizeof(Block) == HDR_SIZE, "Block header size mismatch");

static Block*  g_start = nullptr;
static size_t  g_used  = 0;

void init() {
    uintptr_t start = (uintptr_t)__heap_start;
    uintptr_t end   = (uintptr_t)__heap_end;

    if (end <= start + HDR_SIZE)
        panic("heap: heap region too small");

    g_start = reinterpret_cast<Block*>(start);
    g_start->magic = MAGIC;
    g_start->flags = 1;
    g_start->size  = (end - start) - HDR_SIZE;
    g_start->next  = nullptr;
    g_start->prev  = nullptr;
    g_used = 0;
}

void* alloc(size_t bytes, size_t align) {
    if (!g_start) panic("heap: not initialised");
    if (bytes == 0) bytes = 1;

    bytes = (bytes + 15u) & ~size_t(15u);

    for (Block* b = g_start; b; b = b->next) {
        if (!(b->flags & 1)) continue;

        uintptr_t data_addr = reinterpret_cast<uintptr_t>(b) + HDR_SIZE;
        uintptr_t aligned   = (data_addr + align - 1u) & ~(align - 1u);
        size_t    pad       = aligned - data_addr;

        while (pad > 0 && pad < HDR_SIZE) {
            aligned += align;
            pad      = aligned - data_addr;
        }

        if (b->size < pad + bytes) continue;

        if (pad >= HDR_SIZE) {
            Block* nb   = reinterpret_cast<Block*>(aligned - HDR_SIZE);
            nb->magic   = MAGIC;
            nb->flags   = 1;
            nb->size    = b->size - pad;
            nb->prev    = b;
            nb->next    = b->next;
            if (nb->next) nb->next->prev = nb;
            b->size     = pad - HDR_SIZE;
            b->next     = nb;
            b = nb;
        }

        size_t remaining = b->size - bytes;
        if (remaining >= MIN_SPLIT) {
            Block* tail = reinterpret_cast<Block*>(
                reinterpret_cast<uint8_t*>(b) + HDR_SIZE + bytes);
            tail->magic = MAGIC;
            tail->flags = 1;
            tail->size  = remaining - HDR_SIZE;
            tail->prev  = b;
            tail->next  = b->next;
            if (tail->next) tail->next->prev = tail;
            b->next     = tail;
            b->size     = bytes;
        }

        b->flags = 0;
        g_used  += b->size;
        return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(b) + HDR_SIZE);
    }

    return nullptr;
}

void free(void* ptr) {
    if (!ptr) return;

    Block* b = reinterpret_cast<Block*>(static_cast<uint8_t*>(ptr) - HDR_SIZE);

    if (b->magic != MAGIC)
        panic("heap: corrupt block (bad magic)", reinterpret_cast<uintptr_t>(ptr));
    if (b->flags & 1)
        panic("heap: double free", reinterpret_cast<uintptr_t>(ptr));

    b->flags = 1;
    g_used  -= b->size;

    if (b->next && (b->next->flags & 1)) {
        Block* nx  = b->next;
        b->size   += HDR_SIZE + nx->size;
        b->next    = nx->next;
        if (b->next) b->next->prev = b;
        nx->magic  = 0;
    }

    if (b->prev && (b->prev->flags & 1)) {
        Block* pv  = b->prev;
        pv->size  += HDR_SIZE + b->size;
        pv->next   = b->next;
        if (pv->next) pv->next->prev = pv;
        b->magic   = 0;
    }
}

size_t used_bytes() { return g_used; }

size_t free_bytes() {
    size_t total = 0;
    for (Block* b = g_start; b; b = b->next)
        if (b->flags & 1) total += b->size;
    return total;
}

}
