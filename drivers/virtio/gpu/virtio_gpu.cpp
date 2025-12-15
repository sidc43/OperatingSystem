#include "drivers/virtio/gpu/virtio_gpu.hpp"
#include "drivers/virtio/virtqueue.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#include "kernel/mm/phys/page_alloc.hpp"

namespace virtio_gpu
{
    static constexpr u32 VIRTIO_GPU_CMD_GET_DISPLAY_INFO        = 0x0100;
    static constexpr u32 VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      = 0x0101;
    static constexpr u32 VIRTIO_GPU_CMD_SET_SCANOUT             = 0x0103;
    static constexpr u32 VIRTIO_GPU_CMD_RESOURCE_FLUSH          = 0x0104;
    static constexpr u32 VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     = 0x0105;
    static constexpr u32 VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING = 0x0106;

    static constexpr u32 VIRTIO_GPU_RESP_OK_NODATA              = 0x1100;
    static constexpr u32 VIRTIO_GPU_RESP_OK_DISPLAY_INFO        = 0x1101;

    static constexpr u32 VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM       = 4;

    static constexpr u64 SPIN_LIMIT = 50'000'000ull;

    struct __attribute__((packed)) CtrlHdr
    {
        u32 type;
        u32 flags;
        u64 fence_id;
        u32 ctx_id;
        u32 padding;
    };

    struct __attribute__((packed)) Rect
    {
        u32 x;
        u32 y;
        u32 width;
        u32 height;
    };

    struct __attribute__((packed)) RespOkNoData
    {
        CtrlHdr hdr;
    };

    struct __attribute__((packed)) GetDisplayInfoReq
    {
        CtrlHdr hdr;
    };

    struct __attribute__((packed)) DisplayOne
    {
        Rect r;
        u32 enabled;
        u32 flags;
    };

    struct __attribute__((packed)) GetDisplayInfoResp
    {
        CtrlHdr hdr;
        DisplayOne pmodes[16];
    };

    struct __attribute__((packed)) ResourceCreate2D
    {
        CtrlHdr hdr;
        u32 resource_id;
        u32 format;
        u32 width;
        u32 height;
    };

    struct __attribute__((packed)) MemEntry
    {
        u64 addr;
        u32 length;
        u32 padding;
    };

    struct __attribute__((packed)) ResourceAttachBacking
    {
        CtrlHdr hdr;
        u32 resource_id;
        u32 nr_entries;
    };

    struct __attribute__((packed)) SetScanout
    {
        CtrlHdr hdr;
        Rect r;
        u32 scanout_id;
        u32 resource_id;
    };

    struct __attribute__((packed)) TransferToHost2D
    {
        CtrlHdr hdr;
        Rect r;
        u64 offset;
        u32 resource_id;
        u32 padding;
    };

    struct __attribute__((packed)) ResourceFlush
    {
        CtrlHdr hdr;
        Rect r;
        u32 resource_id;
        u32 padding;
    };

    static inline void zero_bytes(void* p, usize n)
    {
        u8* b = (u8*)p;
        for (usize i = 0; i < n; i++)
        {
            b[i] = 0;
        }
    }

    static virtio::VirtQueue g_ctrlq {};
    static bool g_ctrlq_ready = false;

    static void ctrlq_init_once(const virtio::PciTransport& t)
    {
        if (g_ctrlq_ready)
        {
            return;
        }

        if (!g_ctrlq.init(t, 0))
        {
            panic("virtio-gpu: ctrl queue init failed");
        }

        g_ctrlq_ready = true;
    }

    struct DmaPair
    {
        void* req_page {};
        void* resp_page {};
        u64 req_pa {};
        void* req_va {};
        void* resp_va {};
    };

    static DmaPair dma_pair_alloc()
    {
        DmaPair p {};

        p.req_page  = phys::alloc_pages(1);
        p.resp_page = phys::alloc_pages(1);

        if (!p.req_page || !p.resp_page)
        {
            panic("virtio-gpu: dma alloc failed");
        }

        p.req_pa  = (u64)(uintptr_t)p.req_page;
        p.req_va  = (void*)(uintptr_t)p.req_pa;
        p.resp_va = (void*)(uintptr_t)(u64)(uintptr_t)p.resp_page;

        zero_bytes(p.req_va, 4096);
        zero_bytes(p.resp_va, 4096);

        return p;
    }

    static void wait_used_or_panic(const char* where)
    {
        u16 used_id = 0;
        u32 used_len = 0;

        for (u64 spins = 0; spins < SPIN_LIMIT; spins++)
        {
            if (g_ctrlq.wait_used(&used_id, &used_len))
            {
                return;
            }
        }

        kprint::puts("virtio-gpu: timeout waiting used ring at: ");
        kprint::puts(where);
        kprint::puts("\n");
        panic("virtio-gpu: wait_used timeout");
    }

    static bool submit_expect_nodata(u64 req_pa, u32 req_len, void* resp_va, u32 resp_len, const char* where)
    {
        g_ctrlq.submit_2((const void*)(uintptr_t)req_pa, req_len, resp_va, resp_len);
        wait_used_or_panic(where);

        RespOkNoData* r = (RespOkNoData*)resp_va;
        if (r->hdr.type != VIRTIO_GPU_RESP_OK_NODATA)
        {
            kprint::puts("virtio-gpu: expected OK_NODATA got=");
            kprint::hex_u64(r->hdr.type);
            kprint::puts("\n");
            return false;
        }

        return true;
    }

    bool get_display_info(const virtio::PciTransport& t, DisplayInfo& out)
    {
        ctrlq_init_once(t);

        DmaPair dp = dma_pair_alloc();

        GetDisplayInfoReq* req = (GetDisplayInfoReq*)dp.req_va;
        GetDisplayInfoResp* resp = (GetDisplayInfoResp*)dp.resp_va;

        req->hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

        g_ctrlq.submit_2((const void*)(uintptr_t)dp.req_pa, sizeof(GetDisplayInfoReq), resp, sizeof(GetDisplayInfoResp));
        wait_used_or_panic("GET_DISPLAY_INFO");

        if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
        {
            return false;
        }

        out.width   = resp->pmodes[0].r.width;
        out.height  = resp->pmodes[0].r.height;
        out.enabled = (resp->pmodes[0].enabled != 0);

        return true;
    }

    bool init_framebuffer(const virtio::PciTransport& t, FrameBuffer& fb, u32 w, u32 h)
    {
        ctrlq_init_once(t);

        fb.t = t;
        fb.width = w;
        fb.height = h;
        fb.stride = w * 4;
        fb.resource_id = 1;

        fb.fb_bytes = (u64)w * (u64)h * 4ull;
        u64 pages = (fb.fb_bytes + 4095) / 4096;

        void* fb_ptr = phys::alloc_pages((usize)pages);
        if (!fb_ptr)
        {
            return false;
        }

        fb.fb_pa = (u64)(uintptr_t)fb_ptr;
        fb.fb = (u32*)(uintptr_t)fb.fb_pa;

        {
            DmaPair dp = dma_pair_alloc();
            ResourceCreate2D* c = (ResourceCreate2D*)dp.req_va;
            RespOkNoData* r = (RespOkNoData*)dp.resp_va;

            c->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
            c->resource_id = fb.resource_id;
            c->format = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM;
            c->width = w;
            c->height = h;

            if (!submit_expect_nodata(dp.req_pa, sizeof(ResourceCreate2D), r, sizeof(RespOkNoData), "RESOURCE_CREATE_2D"))
            {
                return false;
            }
        }

        {
            DmaPair dp = dma_pair_alloc();

            struct __attribute__((packed)) AttachReq
            {
                ResourceAttachBacking a;
                MemEntry e;
            };

            AttachReq* a = (AttachReq*)dp.req_va;
            RespOkNoData* r = (RespOkNoData*)dp.resp_va;

            a->a.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
            a->a.resource_id = fb.resource_id;
            a->a.nr_entries = 1;
            a->e.addr = fb.fb_pa;
            a->e.length = (u32)fb.fb_bytes;

            if (!submit_expect_nodata(dp.req_pa, sizeof(AttachReq), r, sizeof(RespOkNoData), "ATTACH_BACKING"))
            {
                return false;
            }
        }

        {
            DmaPair dp = dma_pair_alloc();
            SetScanout* s = (SetScanout*)dp.req_va;
            RespOkNoData* r = (RespOkNoData*)dp.resp_va;

            s->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
            s->r.x = 0;
            s->r.y = 0;
            s->r.width = w;
            s->r.height = h;
            s->scanout_id = 0;
            s->resource_id = fb.resource_id;

            if (!submit_expect_nodata(dp.req_pa, sizeof(SetScanout), r, sizeof(RespOkNoData), "SET_SCANOUT"))
            {
                return false;
            }
        }

        return true;
    }

    void clear(FrameBuffer& fb, u32 color)
    {
        u64 pixels = (fb.fb_bytes / 4);
        for (u64 i = 0; i < pixels; i++)
        {
            fb.fb[i] = color;
        }
    }

    bool present(const FrameBuffer& fb, u32 x, u32 y, u32 w, u32 h)
    {
        ctrlq_init_once(fb.t);

        {
            DmaPair dp = dma_pair_alloc();
            TransferToHost2D* t2h = (TransferToHost2D*)dp.req_va;
            RespOkNoData* r = (RespOkNoData*)dp.resp_va;

            t2h->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
            t2h->r.x = x;
            t2h->r.y = y;
            t2h->r.width = w;
            t2h->r.height = h;
            t2h->offset = 0;
            t2h->resource_id = fb.resource_id;

            if (!submit_expect_nodata(dp.req_pa, sizeof(TransferToHost2D), r, sizeof(RespOkNoData), "TRANSFER_TO_HOST_2D"))
            {
                return false;
            }
        }

        {
            DmaPair dp = dma_pair_alloc();
            ResourceFlush* f = (ResourceFlush*)dp.req_va;
            RespOkNoData* r = (RespOkNoData*)dp.resp_va;

            f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
            f->r.x = x;
            f->r.y = y;
            f->r.width = w;
            f->r.height = h;
            f->resource_id = fb.resource_id;

            if (!submit_expect_nodata(dp.req_pa, sizeof(ResourceFlush), r, sizeof(RespOkNoData), "RESOURCE_FLUSH"))
            {
                return false;
            }
        }

        return true;
    }

    bool solid_color_scanout0_test(const virtio::PciTransport& t)
    {
        FrameBuffer fb {};
        DisplayInfo di {};

        if (!get_display_info(t, di))
        {
            return false;
        }

        if (!init_framebuffer(t, fb, 640, 480))
        {
            return false;
        }

        clear(fb, 0x0030A0FF);
        present(fb, 0, 0, fb.width, fb.height);

        return true;
    }
}
