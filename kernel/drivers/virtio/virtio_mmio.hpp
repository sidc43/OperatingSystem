/*
  virtio_mmio.hpp - virtio-mmio v2 register map and device ids
  qemu virt puts virtio devices at 0x0a000000 + slot * 0x200
  has device ids, magic/version constants, status bits, and the read32/write32 accessors
*/
#pragma once
#include <stdint.h>

namespace virtio {

static constexpr uint32_t DEVICE_NET        = 1;
static constexpr uint32_t DEVICE_BLK        = 2;
static constexpr uint32_t DEVICE_GPU        = 16;
static constexpr uint32_t DEVICE_INPUT      = 18;

static constexpr uint32_t MMIO_MAGIC        = 0x74726976u;
static constexpr uint32_t MMIO_VERSION      = 2;

static constexpr uint32_t STATUS_ACKNOWLEDGE    = 1u;
static constexpr uint32_t STATUS_DRIVER         = 2u;
static constexpr uint32_t STATUS_DRIVER_OK      = 4u;
static constexpr uint32_t STATUS_FEATURES_OK    = 8u;
static constexpr uint32_t STATUS_NEEDS_RESET    = 64u;
static constexpr uint32_t STATUS_FAILED         = 128u;

static constexpr uint32_t VIRTIO_F_VERSION_1    = (1u << 0);

static constexpr uint16_t VRING_DESC_F_NEXT     = 1u;
static constexpr uint16_t VRING_DESC_F_WRITE    = 2u;
static constexpr uint16_t VRING_DESC_F_INDIRECT = 4u;

enum Reg : uint32_t {
    MagicValue          = 0x000,
    Version             = 0x004,
    DeviceID            = 0x008,
    VendorID            = 0x00c,
    DeviceFeatures      = 0x010,
    DeviceFeaturesSel   = 0x014,
    DriverFeatures      = 0x020,
    DriverFeaturesSel   = 0x024,
    QueueSel            = 0x030,
    QueueNumMax         = 0x034,
    QueueNum            = 0x038,
    QueueReady          = 0x044,
    QueueNotify         = 0x050,
    InterruptStatus     = 0x060,
    InterruptACK        = 0x064,
    Status              = 0x070,
    QueueDescLow        = 0x080,
    QueueDescHigh       = 0x084,
    QueueDriverLow      = 0x090,
    QueueDriverHigh     = 0x094,
    QueueDeviceLow      = 0x0a0,
    QueueDeviceHigh     = 0x0a4,
    ConfigGeneration    = 0x0fc,
    Config              = 0x100,
};

inline volatile uint32_t* reg(uintptr_t base, Reg r) {
    return reinterpret_cast<volatile uint32_t*>(base + r);
}
inline uint32_t  read32 (uintptr_t base, Reg r)             { return *reg(base, r); }
inline void      write32(uintptr_t base, Reg r, uint32_t v) { *reg(base, r) = v;    }

inline volatile uint8_t*  cfg8 (uintptr_t base, uint32_t off) {
    return reinterpret_cast<volatile uint8_t*>(base + Config + off);
}

}
