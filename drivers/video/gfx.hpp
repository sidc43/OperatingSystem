#pragma once
#include "types.hpp"
#include "drivers/virtio/virtqueue.hpp"

namespace gfx
{
    bool init(const virtio::PciTransport& gpu_t);

    u32 width();
    u32 height();
    u32 stride_bytes();

    // FRONT buffer (actual scanout memory)
    u32* framebuffer();

    // BACK buffer (where you draw each frame)
    u32* begin_frame();
    void  end_frame();

    void clear(u32 color);

    void mark_dirty(u32 x, u32 y, u32 w, u32 h);
}
