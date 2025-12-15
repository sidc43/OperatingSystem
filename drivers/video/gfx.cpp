#include "drivers/video/gfx.hpp"

#include "drivers/virtio/gpu/virtio_gpu.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/mm/heap/kheap.hpp"

namespace gfx
{
    static virtio_gpu::FrameBuffer g_fb {};
    static bool g_ready = false;

    static u32* g_back = nullptr;

    static bool g_dirty = false;
    static u32 g_dx0 = 0, g_dy0 = 0, g_dx1 = 0, g_dy1 = 0;

    static inline u32 stride_words()
    {
        return g_fb.stride / 4;
    }

    static inline void dirty_reset()
    {
        g_dirty = false;
        g_dx0 = g_dy0 = 0;
        g_dx1 = g_dy1 = 0;
    }

    bool init(const virtio::PciTransport& gpu_t)
    {
        if (g_ready)
        {
            return true;
        }

        virtio_gpu::DisplayInfo di {};
        if (!virtio_gpu::get_display_info(gpu_t, di))
        {
            panic("gfx: get_display_info failed");
        }

        if (!di.enabled || di.width == 0 || di.height == 0)
        {
            panic("gfx: scanout not enabled");
        }

        if (!virtio_gpu::init_framebuffer(gpu_t, g_fb, di.width, di.height))
        {
            panic("gfx: init_framebuffer failed");
        }

        // Allocate backbuffer (same size as scanout)
        u64 bytes = (u64)g_fb.height * (u64)g_fb.stride;
        g_back = (u32*)kheap::kmalloc((usize)bytes);
        if (!g_back)
        {
            panic("gfx: backbuffer alloc failed");
        }

        dirty_reset();
        g_ready = true;
        return true;
    }

    u32 width()        { return g_fb.width; }
    u32 height()       { return g_fb.height; }
    u32 stride_bytes() { return g_fb.stride; }

    u32* framebuffer()
    {
        return g_fb.fb;
    }

    u32* begin_frame()
    {
        return g_back;
    }

    void mark_dirty(u32 x, u32 y, u32 w, u32 h)
    {
        if (w == 0 || h == 0) return;

        u32 x0 = x;
        u32 y0 = y;
        u32 x1 = x + w;
        u32 y1 = y + h;

        if (x1 > g_fb.width)  x1 = g_fb.width;
        if (y1 > g_fb.height) y1 = g_fb.height;

        if (!g_dirty)
        {
            g_dirty = true;
            g_dx0 = x0; g_dy0 = y0; g_dx1 = x1; g_dy1 = y1;
            return;
        }

        if (x0 < g_dx0) g_dx0 = x0;
        if (y0 < g_dy0) g_dy0 = y0;
        if (x1 > g_dx1) g_dx1 = x1;
        if (y1 > g_dy1) g_dy1 = y1;
    }

    void clear(u32 color)
    {
        u32* b = g_back;
        u32 sw = stride_words();

        for (u32 y = 0; y < g_fb.height; y++)
        {
            u32* row = b + y * sw;
            for (u32 x = 0; x < g_fb.width; x++)
            {
                row[x] = color;
            }
        }

        mark_dirty(0, 0, g_fb.width, g_fb.height);
    }

    void end_frame()
    {
        if (!g_dirty)
        {
            return;
        }

        u32 sw = stride_words();

        // Copy only dirty rect from backbuffer -> front framebuffer
        for (u32 y = g_dy0; y < g_dy1; y++)
        {
            u32* src = g_back + y * sw + g_dx0;
            u32* dst = g_fb.fb + y * sw + g_dx0;

            for (u32 x = g_dx0; x < g_dx1; x++)
            {
                *dst++ = *src++;
            }
        }

        // Present only the dirty rect
        virtio_gpu::present(g_fb, g_dx0, g_dy0, (g_dx1 - g_dx0), (g_dy1 - g_dy0));

        dirty_reset();
    }
}
