/*
  blk.cpp - virtio-blk block device driver
  synchronous polling 512-byte sector read and write
  one request in flight at a time, no interrupt needed
*/
#include "kernel/drivers/virtio/blk.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/print.hpp"
#include "arch/aarch64/regs.hpp"
#include <string.h>
#include <stdint.h>

static constexpr uint32_t BLK_T_IN  = 0;
static constexpr uint32_t BLK_T_OUT = 1;

static constexpr uint8_t  BLK_S_OK    = 0;
static constexpr uint8_t  BLK_S_IOERR [[maybe_unused]] = 1;

struct BlkReqHdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

namespace {

static uintptr_t          g_base    = 0;
static bool               g_ready   = false;
static uint64_t           g_sectors = 0;
static virtio::VirtQueue  g_queue;

static BlkReqHdr s_hdr    __attribute__((aligned(16)));
static uint8_t   s_status __attribute__((aligned(4)));

static bool negotiate(uintptr_t base) {
    using namespace virtio;

    write32(base, Status, 0);
    dsb_sy();
    for (int i = 0; i < 1000; ++i)
        if (read32(base, Status) == 0) break;

    write32(base, Status, STATUS_ACKNOWLEDGE);
    dsb_sy();
    write32(base, Status, STATUS_ACKNOWLEDGE | STATUS_DRIVER);
    dsb_sy();

    write32(base, DeviceFeaturesSel, 0); dsb_sy();
    uint32_t f0 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 0); dsb_sy();
    write32(base, DriverFeatures, f0);   dsb_sy();

    write32(base, DeviceFeaturesSel, 1); dsb_sy();
    uint32_t f1 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 1); dsb_sy();
    write32(base, DriverFeatures, f1 | VIRTIO_F_VERSION_1); dsb_sy();

    write32(base, Status,
            STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    dsb_sy();
    if (!(read32(base, Status) & STATUS_FEATURES_OK)) return false;

    return true;
}

static void wait_used() {
    while (!g_queue.poll_used()) { asm volatile("nop"); }
}

}

namespace vblk {

bool init(const uintptr_t* bases, int n) {
    using namespace virtio;

    for (int i = 0; i < n; ++i) {
        uintptr_t base = bases[i];
        if (read32(base, DeviceID) != DEVICE_BLK) continue;

        if (!negotiate(base)) {
            print("vblk: feature negotiation failed\n");
            continue;
        }

        if (!g_queue.init(base, 0)) {
            print("vblk: queue 0 init failed\n");
            continue;
        }

        write32(base, Status,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER |
                STATUS_FEATURES_OK | STATUS_DRIVER_OK);
        dsb_sy();

        if (read32(base, Status) & STATUS_NEEDS_RESET) {
            print("vblk: device needs reset after DRIVER_OK\n");
            continue;
        }

        uint32_t cap_lo = read32(base, (Reg)(Config     ));
        uint32_t cap_hi = read32(base, (Reg)(Config + 4 ));
        g_sectors = ((uint64_t)cap_hi << 32) | cap_lo;

        g_base  = base;
        g_ready = true;

        printk("vblk: found at 0x%x  capacity=%u MiB\n",
               (unsigned)base,
               (unsigned)(g_sectors / 2048));
        return true;
    }
    return false;
}

bool     ready()        { return g_ready;   }
uint64_t sector_count() { return g_sectors; }

bool read_sectors(uint64_t lba, uint32_t count, void* buf) {
    if (!g_ready || !buf || count == 0) return false;

    s_hdr.type     = BLK_T_IN;
    s_hdr.reserved = 0;
    s_hdr.sector   = lba;
    s_status       = 0xFF;
    dsb_sy();

    uint16_t d0 = g_queue.alloc_desc();
    uint16_t d1 = g_queue.alloc_desc();
    uint16_t d2 = g_queue.alloc_desc();
    if (d0 == 0xFFFF || d1 == 0xFFFF || d2 == 0xFFFF) return false;

    g_queue.fill_desc(d0, virtio::VirtQueue::phys(&s_hdr),   sizeof(BlkReqHdr), false, true,  d1);
    g_queue.fill_desc(d1, virtio::VirtQueue::phys(buf),      count * 512u,       true,  true,  d2);
    g_queue.fill_desc(d2, virtio::VirtQueue::phys(&s_status),1u,                 true,  false, 0);
    dsb_sy();

    g_queue.submit(d0, g_base, 0);
    wait_used();

    g_queue.free_desc(d2);
    g_queue.free_desc(d1);
    g_queue.free_desc(d0);
    dsb_sy();

    return (s_status == BLK_S_OK);
}

bool write_sectors(uint64_t lba, uint32_t count, const void* buf) {
    if (!g_ready || !buf || count == 0) return false;

    s_hdr.type     = BLK_T_OUT;
    s_hdr.reserved = 0;
    s_hdr.sector   = lba;
    s_status       = 0xFF;
    dsb_sy();

    uint16_t d0 = g_queue.alloc_desc();
    uint16_t d1 = g_queue.alloc_desc();
    uint16_t d2 = g_queue.alloc_desc();
    if (d0 == 0xFFFF || d1 == 0xFFFF || d2 == 0xFFFF) return false;

    g_queue.fill_desc(d0, virtio::VirtQueue::phys(&s_hdr),   sizeof(BlkReqHdr), false, true,  d1);
    g_queue.fill_desc(d1, virtio::VirtQueue::phys(buf),      count * 512u,       false, true,  d2);
    g_queue.fill_desc(d2, virtio::VirtQueue::phys(&s_status),1u,                 true,  false, 0);
    dsb_sy();

    g_queue.submit(d0, g_base, 0);
    wait_used();

    g_queue.free_desc(d2);
    g_queue.free_desc(d1);
    g_queue.free_desc(d0);
    dsb_sy();

    return (s_status == BLK_S_OK);
}

}
