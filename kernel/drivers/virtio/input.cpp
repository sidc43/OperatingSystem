/*
  input.cpp - virtio-input keyboard driver
  finds the keyboard device, fills the avail ring with event buffers,
  harvests key-press events each poll(), decodes them to ascii via scan.hpp,
  and pushes characters into a small ring buffer
*/
#include "kernel/drivers/virtio/input.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/drivers/virtio/scan.hpp"
#include "kernel/irq/gic.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"
#include "arch/aarch64/regs.hpp"
#include <string.h>
#include <stdint.h>

struct VirtInputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

static constexpr uint16_t EV_KEY  = 0x01;
static constexpr uint16_t EV_ABS  = 0x03;

static constexpr uint32_t KBUF_SIZE = 256;
static char     g_kbuf[KBUF_SIZE];
static uint32_t g_kbuf_head = 0;
static uint32_t g_kbuf_tail = 0;

static void kbuf_push(char c) {
    uint32_t next = (g_kbuf_head + 1) & (KBUF_SIZE - 1);
    if (next != g_kbuf_tail) {
        g_kbuf[g_kbuf_head] = c;
        g_kbuf_head = next;
    }
}

static char kbuf_pop() {
    if (g_kbuf_head == g_kbuf_tail) return 0;
    char c = g_kbuf[g_kbuf_tail];
    g_kbuf_tail = (g_kbuf_tail + 1) & (KBUF_SIZE - 1);
    return c;
}

namespace {

static uintptr_t          g_base   = 0;
static virtio::VirtQueue  g_evtq;
static VirtInputEvent*    g_evbufs = nullptr;
static bool               g_ready  = false;

static bool g_shift = false;
static bool g_caps  = false;
static bool g_ctrl  = false;

static bool negotiate_input(uintptr_t base) {
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
    uint32_t feats0 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 0);
    write32(base, DriverFeatures, feats0);
    dsb_sy();

    write32(base, DeviceFeaturesSel, 1);
    dsb_sy();
    uint32_t feats1 = read32(base, DeviceFeatures);
    write32(base, DriverFeaturesSel, 1);
    write32(base, DriverFeatures, feats1 | VIRTIO_F_VERSION_1);
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

    g_evbufs = static_cast<VirtInputEvent*>(
        kheap::alloc(sizeof(VirtInputEvent) * n, 16));
    if (!g_evbufs) panic("kbd: evbuf alloc failed");
    memset(g_evbufs, 0, sizeof(VirtInputEvent) * n);

    for (uint16_t i = 0; i < n; ++i) {
        g_evtq.desc[i].addr  = VirtQueue::phys(&g_evbufs[i]);
        g_evtq.desc[i].len   = sizeof(VirtInputEvent);
        g_evtq.desc[i].flags = VRING_DESC_F_WRITE;
        g_evtq.desc[i].next  = 0;

        g_evtq.avail->ring[i] = i;
    }
    g_evtq.avail->idx = n;
    dsb_sy();

    write32(g_base, virtio::QueueNotify, 0);
    dsb_sy();
}

}

namespace kbd {

static bool device_has_ev_key(uintptr_t base) {
    volatile uint8_t* sel  = virtio::cfg8(base, 0);
    volatile uint8_t* sub  = virtio::cfg8(base, 1);
    volatile uint8_t* sz   = virtio::cfg8(base, 2);

    *sel = 0x11u;
    *sub = 0x01u;
    dsb_sy();
    if (*sz == 0) return false;

    *sel = 0x11u;
    *sub = (uint8_t)EV_ABS;
    dsb_sy();
    if (*sz > 0) return false;

    return true;
}

bool init(const uintptr_t* bases, int n_bases) {
    using namespace virtio;

    uintptr_t base = 0;
    for (int i = 0; i < n_bases; ++i) {
        uintptr_t b = bases[i];
        if (read32(b, MagicValue) != MMIO_MAGIC)   continue;
        if (read32(b, Version)    != MMIO_VERSION) continue;
        if (read32(b, DeviceID)   != DEVICE_INPUT) continue;
        if (!device_has_ev_key(b))                 continue;
        base = b;
        break;
    }
    if (!base) {
        print("kbd: no virtio-keyboard device found\n");
        return false;
    }
    g_base = base;
    printk("kbd: found device at 0x%x\n", (unsigned)base);

    if (!negotiate_input(base)) {
        print("kbd: feature negotiation failed\n");
        return false;
    }

    if (!g_evtq.init(base, 0)) {
        print("kbd: eventq init failed\n");
        return false;
    }

    write32(base, Status,
            STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);
    dsb_sy();

    if (read32(base, Status) & STATUS_NEEDS_RESET) {
        print("kbd: device set NEEDS_RESET after DRIVER_OK\n");
        return false;
    }

    prefill_evtq();

    uint32_t slot = (uint32_t)((base - 0x0a000000u) / 0x200u);
    uint32_t irq  = 32u + slot;
    gic::register_handler(irq, [](){

        uint32_t isr = read32(g_base, virtio::InterruptStatus);
        if (isr) write32(g_base, virtio::InterruptACK, isr);
        dsb_sy();

    });
    gic::enable_irq(irq);

    g_ready = true;
    printk("kbd: init done (virtio IRQ %u)\n", irq);
    return true;
}

void poll() {
    if (!g_ready) return;

    dsb_sy();
    uint32_t isr = read32(g_base, virtio::InterruptStatus);
    if (isr) write32(g_base, virtio::InterruptACK, isr);
    dsb_sy();

    uint16_t used_idx = g_evtq.used->idx;
    uint16_t last     = g_evtq._last_used;

    while (last != used_idx) {
        uint32_t desc_id = g_evtq.used->ring[last & (g_evtq._num - 1u)].id;
        last++;

        const VirtInputEvent& ev = g_evbufs[desc_id];

        if (ev.type == EV_KEY) {
            uint16_t code = ev.code;
            uint32_t val  = ev.value;

            if (scan::is_shift(code)) {
                g_shift = (val != 0);
            } else if (scan::is_ctrl(code)) {
                g_ctrl = (val != 0);
            } else if (code == scan::KEY_CAPSLOCK && val == 1) {
                g_caps = !g_caps;
            } else if (val == 1 || val == 2) {

                if      (code == scan::KEY_UP)       kbuf_push((char)0x80);
                else if (code == scan::KEY_DOWN)     kbuf_push((char)0x81);
                else if (code == scan::KEY_LEFT)     kbuf_push((char)0x82);
                else if (code == scan::KEY_RIGHT)    kbuf_push((char)0x83);
                else if (code == scan::KEY_HOME)     kbuf_push((char)0x84);
                else if (code == scan::KEY_END)      kbuf_push((char)0x85);
                else if (code == scan::KEY_PAGEUP)   kbuf_push((char)0x86);
                else if (code == scan::KEY_PAGEDOWN) kbuf_push((char)0x87);
                else if (code == scan::KEY_DELETE)   kbuf_push((char)0x88);
                else {
                    char c = scan::to_ascii(code, g_shift, g_caps);
                    if (c) {

                        if (g_ctrl) {
                            if      (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
                            else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
                        }
                        kbuf_push(c);
                    }
                }
            }
        }

        uint16_t slot = g_evtq.avail->idx & (g_evtq._num - 1u);
        g_evtq.avail->ring[slot] = (uint16_t)desc_id;
        dsb_sy();
        g_evtq.avail->idx++;
        dsb_sy();
        write32(g_base, virtio::QueueNotify, 0);
    }
    g_evtq._last_used = last;
}

char getc_nb() {
    poll();
    return kbuf_pop();
}

char getc() {
    char c;
    while (!(c = kbuf_pop())) poll();
    return c;
}

bool ready() { return g_ready; }

}
