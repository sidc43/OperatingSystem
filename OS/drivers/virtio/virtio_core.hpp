#pragma once
#include "types.hpp"

namespace virtio
{
    constexpr u8 STATUS_ACKNOWLEDGE = 1u << 0;
    constexpr u8 STATUS_DRIVER = 1u << 1;
    constexpr u8 STATUS_DRIVER_OK = 1u << 2;
    constexpr u8 STATUS_FEATURES_OK = 1u << 3;
    constexpr u8 STATUS_FAILED = 1u << 7;

    constexpr u64 F_VERSION_1 = 1ull << 32;

    inline void dmb_ish()
    {
        asm volatile("dmb ish" ::: "memory");
    }

    inline void dmb_ishst()
    {
        asm volatile("dmb ishst" ::: "memory");
    }

    inline void dsb_ish()
    {
        asm volatile("dsb ish" ::: "memory");
    }

    template <typename T>
    inline T mmio_read(volatile T* p)
    {
        T v = *p;
        dmb_ish();
        return v;
    }

    template <typename T>
    inline void mmio_write(volatile T* p, T v)
    {
        dmb_ishst();
        *p = v;
        dsb_ish();
    }

    struct __attribute__((packed)) PciCommonCfg
    {
        volatile u32 device_feature_select;
        volatile u32 device_feature;
        volatile u32 driver_feature_select;
        volatile u32 driver_feature;
        volatile u16 msix_config;
        volatile u16 num_queues;
        volatile u8  device_status;
        volatile u8  config_generation;
        volatile u16 queue_select;
        volatile u16 queue_size;
        volatile u16 queue_msix_vector;
        volatile u16 queue_enable;
        volatile u16 queue_notify_off;
        volatile u32 queue_desc_lo;
        volatile u32 queue_desc_hi;
        volatile u32 queue_avail_lo;
        volatile u32 queue_avail_hi;
        volatile u32 queue_used_lo;
        volatile u32 queue_used_hi;
    };

    u64 read_device_features(volatile PciCommonCfg* c);
    void write_driver_features(volatile PciCommonCfg* c, u64 features);
    bool init_modern_device(volatile PciCommonCfg* c, u64 features_to_accept);
}
