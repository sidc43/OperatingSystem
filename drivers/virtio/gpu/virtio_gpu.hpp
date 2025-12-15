#pragma once
#include "types.hpp"
#include "drivers/virtio/virtqueue.hpp"

namespace virtio_gpu
{
    struct DisplayInfo
    {
        u32 width {};
        u32 height {};
        bool enabled {};
    };

    struct FrameBuffer
    {
        virtio::PciTransport t {};

        u32 width {};
        u32 height {};
        u32 stride {};

        u32 resource_id {};
        u64 fb_pa {};
        u32* fb {};
        u64 fb_bytes {};
    };

    bool get_display_info(const virtio::PciTransport& t, DisplayInfo& out);

    bool init_framebuffer(const virtio::PciTransport& t, FrameBuffer& fb, u32 w, u32 h);
    void clear(FrameBuffer& fb, u32 color);
    bool present(const FrameBuffer& fb, u32 x, u32 y, u32 w, u32 h);

    bool solid_color_scanout0_test(const virtio::PciTransport& t);
}
