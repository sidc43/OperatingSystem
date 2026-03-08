/*
  virtqueue.cpp - virtqueue implementation
  init allocates and zeros the descriptor/avail/used rings
  begin_chain/add_desc/submit_chain/notify encode a request, poll() spins until done
*/
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/panic.hpp"
#include "arch/aarch64/regs.hpp"
#include <string.h>
#include <stdint.h>
#include <stddef.h>

namespace virtio {

bool VirtQueue::init(uintptr_t mmio_base, uint16_t queue_idx, uint16_t num) {

    write32(mmio_base, QueueSel, queue_idx);
    dsb_sy();

    uint32_t max_num = read32(mmio_base, QueueNumMax);
    if (max_num == 0) return false;
    if (num > max_num) num = (uint16_t)max_num;
    if (num > QUEUE_SIZE)  num = QUEUE_SIZE;

    _num = num;

    desc  = static_cast<VirtqDesc*> (kheap::alloc(sizeof(VirtqDesc)  * num,  4096));
    avail = static_cast<VirtqAvail*>(kheap::alloc(sizeof(VirtqAvail),         4096));
    used  = static_cast<VirtqUsed*> (kheap::alloc(sizeof(VirtqUsed),          4096));

    if (!desc || !avail || !used)
        panic("virtqueue: alloc failed");

    memset(desc,  0, sizeof(VirtqDesc)  * num);
    memset(avail, 0, sizeof(VirtqAvail));
    memset(used,  0, sizeof(VirtqUsed));

    for (uint16_t i = 0; i < num - 1; ++i)
        desc[i].next = (uint16_t)(i + 1);
    desc[num - 1].next = 0xFFFF;
    _free_head = 0;
    _last_used = 0;

    avail->flags = 0;
    avail->idx   = 0;
    used->flags  = 0;
    used->idx    = 0;

    dsb_sy();

    write32(mmio_base, QueueNum, num);
    dsb_sy();

    uint64_t desc_pa  = phys(desc);
    uint64_t avail_pa = phys(avail);
    uint64_t used_pa  = phys(used);

    write32(mmio_base, QueueDescLow,    (uint32_t)(desc_pa  & 0xFFFFFFFFu));
    write32(mmio_base, QueueDescHigh,   (uint32_t)(desc_pa  >> 32));
    write32(mmio_base, QueueDriverLow,  (uint32_t)(avail_pa & 0xFFFFFFFFu));
    write32(mmio_base, QueueDriverHigh, (uint32_t)(avail_pa >> 32));
    write32(mmio_base, QueueDeviceLow,  (uint32_t)(used_pa  & 0xFFFFFFFFu));
    write32(mmio_base, QueueDeviceHigh, (uint32_t)(used_pa  >> 32));
    dsb_sy();

    write32(mmio_base, QueueReady, 1);
    dsb_sy();

    return true;
}

uint16_t VirtQueue::alloc_desc() {
    if (_free_head == 0xFFFF) return 0xFFFF;
    uint16_t idx = _free_head;
    _free_head = desc[idx].next;
    return idx;
}

void VirtQueue::free_desc(uint16_t idx) {
    desc[idx].flags = 0;
    desc[idx].next  = _free_head;
    _free_head      = idx;
}

void VirtQueue::fill_desc(uint16_t idx, uint64_t pa, uint32_t len,
                           bool write, bool has_next, uint16_t next) {
    desc[idx].addr  = pa;
    desc[idx].len   = len;
    desc[idx].flags = (uint16_t)((write ? VRING_DESC_F_WRITE : 0u) |
                                  (has_next ? VRING_DESC_F_NEXT  : 0u));
    desc[idx].next  = has_next ? next : 0;
}

void VirtQueue::submit(uint16_t head, uintptr_t mmio_base, uint16_t queue_idx) {
    uint16_t slot = avail->idx & (uint16_t)(_num - 1u);
    avail->ring[slot] = head;
    dsb_sy();
    avail->idx = (uint16_t)(avail->idx + 1u);
    dsb_sy();

    dc_civac_range(desc,  sizeof(VirtqDesc) * _num);
    dc_civac_range(avail, sizeof(VirtqAvail));
    write32(mmio_base, QueueNotify, queue_idx);
}

bool VirtQueue::poll_used() {

    dc_ivac_range(used, sizeof(VirtqUsed));
    if (used->idx == _last_used) return false;
    _last_used = used->idx;
    return true;
}

}
