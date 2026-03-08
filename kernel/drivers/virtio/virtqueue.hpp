/*
  virtqueue.hpp - split-ring virtqueue (virtio spec 2.7)
  manages one virtqueue for one virtio device
  all rings heap-allocated with 4096-byte alignment, polling mode (no irq needed)
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace virtio {

static constexpr uint16_t QUEUE_SIZE = 64;

struct VirtqDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct VirtqUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct VirtqUsed {
    uint16_t        flags;
    uint16_t        idx;
    VirtqUsedElem   ring[QUEUE_SIZE];
    uint16_t        avail_event;
} __attribute__((packed));

class VirtQueue {
public:

    bool init(uintptr_t mmio_base, uint16_t queue_idx, uint16_t num = QUEUE_SIZE);

    uint16_t alloc_desc();

    void     free_desc(uint16_t idx);

    void fill_desc(uint16_t idx, uint64_t phys, uint32_t len,
                   bool write, bool has_next, uint16_t next = 0);

    void submit(uint16_t head, uintptr_t mmio_base, uint16_t queue_idx);

    bool poll_used();

    static uint64_t phys(const void* p) {
        return reinterpret_cast<uint64_t>(p);
    }

    VirtqDesc*  desc  = nullptr;
    VirtqAvail* avail = nullptr;
    VirtqUsed*  used  = nullptr;

    uint16_t _free_head = 0;
    uint16_t _last_used = 0;
    uint16_t _num       = 0;
};

}
