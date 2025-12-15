#pragma once
#include "types.hpp"

#include "kernel/bus/pci/pci.hpp"
#include "drivers/virtio/virtqueue.hpp"

namespace virtio_pci
{
    // Virtio vendor capability types
    static constexpr u8 VIRTIO_PCI_CAP_COMMON_CFG = 1;
    static constexpr u8 VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
    static constexpr u8 VIRTIO_PCI_CAP_ISR_CFG    = 3;
    static constexpr u8 VIRTIO_PCI_CAP_DEVICE_CFG = 4;
    static constexpr u8 VIRTIO_PCI_CAP_PCI_CFG    = 5;

    struct Caps
    {
        u8  common_bar {};
        u32 common_off {};
        u32 common_len {};

        u8  notify_bar {};
        u32 notify_off {};
        u32 notify_len {};
        u32 notify_off_multiplier {};

        u8  isr_bar {};
        u32 isr_off {};
        u32 isr_len {};

        u8  dev_bar {};
        u32 dev_off {};
        u32 dev_len {};
    };

    struct Device
    {
        pci::Bdf bdf {};
        u16 vendor {};
        u16 device {};
        u16 subsys_vendor {};
        u16 subsys_id {};
        Caps caps {};
    };

    void scan_and_print(const pci::Ecam& ecam);

    // Build transport from exact BDF (used by your kernel test)
    bool build_transport(const pci::Ecam& ecam, pci::Bdf bdf, virtio::PciTransport& out);

    // Convenience: find first device with matching pci DID and build transport
    bool find_transport_by_pci_did(const pci::Ecam& ecam, u16 target_pci_did, virtio::PciTransport& out);
}
