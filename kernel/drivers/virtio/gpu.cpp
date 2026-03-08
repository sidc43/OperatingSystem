/*
  gpu.cpp - virtio-gpu driver
  finds the gpu device, negotiates features, gets a display, allocates a
  host-visible framebuffer resource, and sets it as the scanout
  flush_rect and flush_full send updated pixel regions to the screen
*/
#include "kernel/drivers/virtio/gpu.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"
#include "arch/aarch64/regs.hpp"
#include <string.h>
#include <stdint.h>

static constexpr uint32_t VCMD_GET_DISPLAY_INFO       = 0x0100u;
static constexpr uint32_t VCMD_RESOURCE_CREATE_2D     = 0x0101u;
static constexpr uint32_t VCMD_RESOURCE_UNREF         = 0x0102u;
static constexpr uint32_t VCMD_SET_SCANOUT            = 0x0103u;
static constexpr uint32_t VCMD_RESOURCE_FLUSH         = 0x0104u;
static constexpr uint32_t VCMD_TRANSFER_TO_HOST_2D    = 0x0105u;
static constexpr uint32_t VCMD_RESOURCE_ATTACH_BACKING= 0x0106u;
static constexpr uint32_t VCMD_RESOURCE_DETACH_BACKING= 0x0107u;

static constexpr uint32_t VRSP_OK_NODATA              = 0x1100u;
static constexpr uint32_t VRSP_OK_DISPLAY_INFO        = 0x1101u;

static constexpr uint32_t VFMT_B8G8R8X8_UNORM         = 2u;

struct VgpuCtrlHdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct VgpuRect {
    uint32_t x, y, w, h;
} __attribute__((packed));

struct VgpuDisplayOne {
    VgpuRect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

static constexpr int VGPU_MAX_SCANOUTS = 16;

struct VgpuRspDisplayInfo {
    VgpuCtrlHdr   hdr;
    VgpuDisplayOne pmodes[VGPU_MAX_SCANOUTS];
} __attribute__((packed));

struct VgpuResourceCreate2D {
    VgpuCtrlHdr hdr;
    uint32_t    resource_id;
    uint32_t    format;
    uint32_t    width;
    uint32_t    height;
} __attribute__((packed));

struct VgpuMemEntry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct VgpuAttachBacking {
    VgpuCtrlHdr hdr;
    uint32_t    resource_id;
    uint32_t    nr_entries;
    VgpuMemEntry entries[1];
} __attribute__((packed));

struct VgpuSetScanout {
    VgpuCtrlHdr hdr;
    VgpuRect    r;
    uint32_t    scanout_id;
    uint32_t    resource_id;
} __attribute__((packed));

struct VgpuTransferToHost2D {
    VgpuCtrlHdr hdr;
    VgpuRect    r;
    uint64_t    offset;
    uint32_t    resource_id;
    uint32_t    padding;
} __attribute__((packed));

struct VgpuResourceFlush {
    VgpuCtrlHdr hdr;
    VgpuRect    r;
    uint32_t    resource_id;
    uint32_t    padding;
} __attribute__((packed));

namespace {

static uintptr_t         g_base    = 0;
static virtio::VirtQueue g_ctrlq;
static uint32_t*         g_fb      = nullptr;
static uint32_t          g_width   = 0;
static uint32_t          g_height  = 0;
static bool              g_ready   = false;

static VgpuCtrlHdr          s_cmd_hdr      __attribute__((aligned(16)));
static VgpuCtrlHdr          s_rsp_hdr      __attribute__((aligned(16)));
static VgpuRspDisplayInfo   s_rsp_display  __attribute__((aligned(16)));
static VgpuResourceCreate2D s_cmd_create   __attribute__((aligned(16)));
static VgpuAttachBacking    s_cmd_attach   __attribute__((aligned(16)));
static VgpuSetScanout       s_cmd_scanout  __attribute__((aligned(16)));
static VgpuTransferToHost2D s_cmd_transfer __attribute__((aligned(16)));
static VgpuResourceFlush    s_cmd_flush    __attribute__((aligned(16)));

static void wait_used() {
    for (int i = 0; i < 10'000'000; ++i) {
        dsb_sy();
        if (g_ctrlq.poll_used()) return;
    }
    panic("vgpu: command timeout");
}

static void send(const void* cmd, uint32_t cmd_len,
                 void*       rsp, uint32_t rsp_len) {
    uint16_t d0 = g_ctrlq.alloc_desc();
    uint16_t d1 = g_ctrlq.alloc_desc();
    if (d0 == 0xFFFF || d1 == 0xFFFF) panic("vgpu: out of descriptors");

    g_ctrlq.fill_desc(d0, virtio::VirtQueue::phys(cmd), cmd_len, false, true,  d1);
    g_ctrlq.fill_desc(d1, virtio::VirtQueue::phys(rsp), rsp_len, true,  false, 0);

    g_ctrlq.submit(d0, g_base, 0);
    wait_used();

    g_ctrlq.free_desc(d1);
    g_ctrlq.free_desc(d0);
}

static bool negotiate(uintptr_t base) {
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

    write32(base, Status, STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    dsb_sy();

    uint32_t s = read32(base, Status);
    if (!(s & STATUS_FEATURES_OK)) {
        printk("vgpu: FEATURES_OK not set (status=0x%x)\n", s);
        return false;
    }

    return true;
}

}

namespace vgpu {

bool init(const uintptr_t* bases, int n_bases) {
    using namespace virtio;

    uintptr_t base = 0;
    for (int i = 0; i < n_bases; ++i) {
        uintptr_t b = bases[i];
        if (read32(b, MagicValue) != MMIO_MAGIC)    continue;
        if (read32(b, Version)    != MMIO_VERSION)  continue;
        if (read32(b, DeviceID)   != DEVICE_GPU)    continue;
        base = b;
        break;
    }
    if (!base) {
        print("vgpu: no virtio-gpu device found\n");
        return false;
    }
    g_base = base;
    printk("vgpu: found device at 0x%x\n", (unsigned)base);

    if (!negotiate(base)) return false;

    if (!g_ctrlq.init(base, 0)) {
        print("vgpu: controlq init failed\n");
        return false;
    }

    write32(base, Status,
            STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);
    dsb_sy();

    {
        memset(&s_cmd_hdr,     0, sizeof(s_cmd_hdr));
        memset(&s_rsp_display, 0, sizeof(s_rsp_display));
        s_cmd_hdr.type = VCMD_GET_DISPLAY_INFO;
        send(&s_cmd_hdr, sizeof(s_cmd_hdr), &s_rsp_display, sizeof(s_rsp_display));

        if (s_rsp_display.hdr.type != VRSP_OK_DISPLAY_INFO) {
            print("vgpu: GET_DISPLAY_INFO failed\n");
            return false;
        }

        g_width  = s_rsp_display.pmodes[0].r.w;
        g_height = s_rsp_display.pmodes[0].r.h;
        for (int i = 0; i < VGPU_MAX_SCANOUTS; ++i) {
            if (s_rsp_display.pmodes[i].enabled && s_rsp_display.pmodes[i].r.w && s_rsp_display.pmodes[i].r.h) {
                g_width  = s_rsp_display.pmodes[i].r.w;
                g_height = s_rsp_display.pmodes[i].r.h;
                break;
            }
        }
        if (!g_width || !g_height) { g_width = 1024; g_height = 768; }
        printk("vgpu: display %u x %u\n", g_width, g_height);
    }

    uint32_t fb_bytes = g_width * g_height * 4u;
    g_fb = static_cast<uint32_t*>(kheap::alloc(fb_bytes, 4096));
    if (!g_fb) panic("vgpu: framebuffer alloc failed");

    memset(g_fb, 0, fb_bytes);

    {
        memset(&s_cmd_create, 0, sizeof(s_cmd_create));
        memset(&s_rsp_hdr,    0, sizeof(s_rsp_hdr));
        s_cmd_create.hdr.type    = VCMD_RESOURCE_CREATE_2D;
        s_cmd_create.resource_id = 1;
        s_cmd_create.format      = VFMT_B8G8R8X8_UNORM;
        s_cmd_create.width       = g_width;
        s_cmd_create.height      = g_height;
        send(&s_cmd_create, sizeof(s_cmd_create), &s_rsp_hdr, sizeof(s_rsp_hdr));
        if (s_rsp_hdr.type != VRSP_OK_NODATA) {
            print("vgpu: RESOURCE_CREATE_2D failed\n");
            return false;
        }
    }

    {
        memset(&s_cmd_attach, 0, sizeof(s_cmd_attach));
        memset(&s_rsp_hdr,    0, sizeof(s_rsp_hdr));
        s_cmd_attach.hdr.type    = VCMD_RESOURCE_ATTACH_BACKING;
        s_cmd_attach.resource_id = 1;
        s_cmd_attach.nr_entries  = 1;
        s_cmd_attach.entries[0].addr   = VirtQueue::phys(g_fb);
        s_cmd_attach.entries[0].length = fb_bytes;
        send(&s_cmd_attach, sizeof(s_cmd_attach), &s_rsp_hdr, sizeof(s_rsp_hdr));
        if (s_rsp_hdr.type != VRSP_OK_NODATA) {
            print("vgpu: RESOURCE_ATTACH_BACKING failed\n");
            return false;
        }
    }

    {
        memset(&s_cmd_scanout, 0, sizeof(s_cmd_scanout));
        memset(&s_rsp_hdr,     0, sizeof(s_rsp_hdr));
        s_cmd_scanout.hdr.type    = VCMD_SET_SCANOUT;
        s_cmd_scanout.r           = { 0, 0, g_width, g_height };
        s_cmd_scanout.scanout_id  = 0;
        s_cmd_scanout.resource_id = 1;
        send(&s_cmd_scanout, sizeof(s_cmd_scanout), &s_rsp_hdr, sizeof(s_rsp_hdr));
        if (s_rsp_hdr.type != VRSP_OK_NODATA) {
            print("vgpu: SET_SCANOUT failed\n");
            return false;
        }
    }

    g_ready = true;
    print("vgpu: init done\n");
    return true;
}

uint32_t* framebuffer() { return g_fb;     }
uint32_t  width()       { return g_width;  }
uint32_t  height()      { return g_height; }
bool      ready()       { return g_ready;  }

void flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g_ready) return;

    {
        memset(&s_cmd_transfer, 0, sizeof(s_cmd_transfer));
        memset(&s_rsp_hdr,      0, sizeof(s_rsp_hdr));
        s_cmd_transfer.hdr.type    = VCMD_TRANSFER_TO_HOST_2D;
        s_cmd_transfer.r           = { x, y, w, h };
        s_cmd_transfer.offset      = (uint64_t)(y * g_width + x) * 4u;
        s_cmd_transfer.resource_id = 1;
        send(&s_cmd_transfer, sizeof(s_cmd_transfer), &s_rsp_hdr, sizeof(s_rsp_hdr));
    }

    {
        memset(&s_cmd_flush, 0, sizeof(s_cmd_flush));
        memset(&s_rsp_hdr,   0, sizeof(s_rsp_hdr));
        s_cmd_flush.hdr.type    = VCMD_RESOURCE_FLUSH;
        s_cmd_flush.r           = { x, y, w, h };
        s_cmd_flush.resource_id = 1;
        send(&s_cmd_flush, sizeof(s_cmd_flush), &s_rsp_hdr, sizeof(s_rsp_hdr));
    }
}

void flush_full() {
    if (g_ready) flush_rect(0, 0, g_width, g_height);
}

}
