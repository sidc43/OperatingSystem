#pragma once
#include "drivers/virtio/virtqueue.hpp"

namespace tests
{
    void print_test();
    void tty_test();

    void fault_test();

    void el_test();

    void timer_test();

    void phys_alloc_test();
    void mmu_test();
    void heap_test();
    void vm_map_unmap_test();
    void demand_paging_test();

    void virtio_gpu_displayinfo_test(const virtio::PciTransport& t);
    void virtio_gpu_solid_color_test(const virtio::PciTransport& t);
    void virtio_gpu_gfx_gradient_test(const virtio::PciTransport& gpu_t);

    // Pass the two virtio-input transports (typically keyboard + mouse)
    void virtio_input_poll_two_test(const virtio::PciTransport& in0, const virtio::PciTransport& in1);
    void gui_cursor_test(const virtio::PciTransport& gpu_t, const virtio::PciTransport& in0, const virtio::PciTransport& in1);

    void gui_terminal_test(const virtio::PciTransport& gpu_t,
                       const virtio::PciTransport& in0,
                       const virtio::PciTransport& in1);

}
