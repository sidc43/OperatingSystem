/*
  tablet.cpp - virtio-input tablet/mouse driver (absolute coordinates)
  probes ev_abs support to distinguish from keyboard device
  raw coordinates are in 0-0x7fff range, scaled to screen pixels
*/
#include "kernel/drivers/virtio/tablet.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/irq/gic.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"
#include "arch/aarch64/regs.hpp"
#include <string.h>
#include <stdint.h>

struct TabInputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

static constexpr uint16_t TEV_KEY = 0x01;
static constexpr uint16_t TEV_ABS = 0x03;

static constexpr uint16_t ABS_X   = 0x00;
static constexpr uint16_t ABS_Y   = 0x01;

static constexpr uint16_t BTN_LEFT  = 0x110;
static constexpr uint16_t BTN_RIGHT = 0x111;

namespace {

static uintptr_t         g_base    = 0;
static virtio::VirtQueue g_evtq;
static TabInputEvent*    g_evbufs  = nullptr;
static bool              g_ready   = false;

static uint32_t g_scr_w = 1280;
static uint32_t g_scr_h = 800;

static int32_t g_raw_x  = 0;
static int32_t g_raw_y  = 0;
static int32_t g_pix_x  = 0;
static int32_t g_pix_y  = 0;

static bool g_btn_left  = false;
static bool g_btn_right = false;

static bool device_has_ev_abs(uintptr_t base) {
    volatile uint8_t* sel = virtio::cfg8(base, 0);
    volatile uint8_t* sub = virtio::cfg8(base, 1);
    volatile uint8_t* sz  = virtio::cfg8(base, 2);
    *sel = 0x11u;
    *sub = 0x03u;
    dsb_sy();
    return *sz > 0;
}

static bool negotiate_tablet(uintptr_t base) {
    using namespace virtio;

    write32(base, Status, 0);
    dsb_sy();
    for (int i = 0; i < 1000; ++i) {
        if (read32(base, Status) == 0) break;
    }

    write32(base, Status, STATUS_ACKNOWLEDGE);
    dsb_sy();
    write32(base, Status, STATUS_ACKNOWLEDGE | STATUS_DRIVER);
    dsb_sy();

    write32(base, DeviceFeaturesSel, 0);
    dsb_sy();
    uint32_t f0 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 0);
    write32(base, DriverFeatures, f0);
    dsb_sy();

    write32(base, DeviceFeaturesSel, 1);
    dsb_sy();
    uint32_t f1 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 1);
    write32(base, DriverFeatures, f1 | VIRTIO_F_VERSION_1);
    dsb_sy();

    write32(base, Status,
            STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    dsb_sy();

    if (!(read32(base, Status) & STATUS_FEATURES_OK)) return false;
    return true;
}

static void prefill_evtq() {
    using namespace virtio;

    uint16_t n = g_evtq._num;

    g_evbufs = static_cast<TabInputEvent*>(
        kheap::alloc(sizeof(TabInputEvent) * n, 16));
    if (!g_evbufs) panic("tablet: evbuf alloc failed");
    memset(g_evbufs, 0, sizeof(TabInputEvent) * n);

    for (uint16_t i = 0; i < n; ++i) {
        g_evtq.desc[i].addr  = virtio::VirtQueue::phys(&g_evbufs[i]);
        g_evtq.desc[i].len   = sizeof(TabInputEvent);
        g_evtq.desc[i].flags = VRING_DESC_F_WRITE;
        g_evtq.desc[i].next  = 0;
        g_evtq.avail->ring[i] = i;
    }
    g_evtq.avail->idx = n;
    dsb_sy();

    write32(g_base, virtio::QueueNotify, 0);
    dsb_sy();
}

static int32_t raw_to_pix(int32_t raw, uint32_t screen_dim) {

    return (int32_t)((uint32_t)raw * screen_dim >> 15);
}

}

namespace tablet {

bool init(const uintptr_t* bases, int n_bases,
          uint32_t screen_w, uint32_t screen_h) {
    using namespace virtio;

    g_scr_w = screen_w;
    g_scr_h = screen_h;

    uintptr_t base = 0;
    for (int i = 0; i < n_bases; ++i) {
        uintptr_t b = bases[i];
        if (read32(b, MagicValue) != MMIO_MAGIC)   continue;
        if (read32(b, Version)    != MMIO_VERSION) continue;
        if (read32(b, DeviceID)   != DEVICE_INPUT) continue;
        if (!device_has_ev_abs(b))                 continue;
        base = b;
        break;
    }
    if (!base) {
        print("tablet: no virtio-tablet device found\n");
        return false;
    }
    g_base = base;
    printk("tablet: found device at 0x%x\n", (unsigned)base);

    if (!negotiate_tablet(base)) {
        print("tablet: feature negotiation failed\n");
        return false;
    }

    if (!g_evtq.init(base, 0)) {
        print("tablet: eventq init failed\n");
        return false;
    }

    write32(base, Status,
            STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);
    dsb_sy();

    if (read32(base, Status) & STATUS_NEEDS_RESET) {
        print("tablet: device set NEEDS_RESET after DRIVER_OK\n");
        return false;
    }

    prefill_evtq();

    uint32_t slot = (uint32_t)((base - 0x0a000000u) / 0x200u);
    uint32_t irq  = 32u + slot;
    gic::register_handler(irq, [](){
        uint32_t isr = virtio::read32(g_base, virtio::InterruptStatus);
        if (isr) virtio::write32(g_base, virtio::InterruptACK, isr);
        dsb_sy();
    });
    gic::enable_irq(irq);

    g_ready = true;
    printk("tablet: init done (virtio IRQ %u)\n", irq);
    return true;
}

void poll() {
    if (!g_ready) return;

    dsb_sy();
    uint32_t isr = virtio::read32(g_base, virtio::InterruptStatus);
    if (isr) virtio::write32(g_base, virtio::InterruptACK, isr);
    dsb_sy();

    uint16_t used_idx = g_evtq.used->idx;
    uint16_t last     = g_evtq._last_used;

    while (last != used_idx) {
        uint32_t desc_id = g_evtq.used->ring[last & (g_evtq._num - 1u)].id;
        ++last;

        const TabInputEvent& ev = g_evbufs[desc_id];

        if (ev.type == TEV_ABS) {
            if (ev.code == ABS_X) {
                g_raw_x = (int32_t)ev.value;
                g_pix_x = raw_to_pix(g_raw_x, g_scr_w);
            } else if (ev.code == ABS_Y) {
                g_raw_y = (int32_t)ev.value;
                g_pix_y = raw_to_pix(g_raw_y, g_scr_h);
            }
        } else if (ev.type == TEV_KEY) {
            if (ev.code == BTN_LEFT)  g_btn_left  = (ev.value != 0);
            if (ev.code == BTN_RIGHT) g_btn_right = (ev.value != 0);
        }

        uint16_t slot = g_evtq.avail->idx & (g_evtq._num - 1u);
        g_evtq.avail->ring[slot] = (uint16_t)desc_id;
        dsb_sy();
        g_evtq.avail->idx++;
        dsb_sy();
        virtio::write32(g_base, virtio::QueueNotify, 0);
    }
    g_evtq._last_used = last;
}

int32_t cx()         { return g_pix_x;     }
int32_t cy()         { return g_pix_y;     }
bool    btn_left()   { return g_btn_left;  }
bool    btn_right()  { return g_btn_right; }
bool    ready()      { return g_ready;     }

}
