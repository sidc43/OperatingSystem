#include "drivers/virtio/virtio_core.hpp"

namespace virtio
{
    u64 read_device_features(volatile PciCommonCfg* c)
    {
        mmio_write(&c->device_feature_select, 0u);
        u32 lo = mmio_read(&c->device_feature);

        mmio_write(&c->device_feature_select, 1u);
        u32 hi = mmio_read(&c->device_feature);

        return (u64(hi) << 32) | u64(lo);
    }

    void write_driver_features(volatile PciCommonCfg* c, u64 features)
    {
        mmio_write(&c->driver_feature_select, 0u);
        mmio_write(&c->driver_feature, u32(features & 0xFFFFFFFFull));

        mmio_write(&c->driver_feature_select, 1u);
        mmio_write(&c->driver_feature, u32((features >> 32) & 0xFFFFFFFFull));
    }

    bool init_modern_device(volatile PciCommonCfg* c, u64 features_to_accept)
    {
        mmio_write(&c->device_status, u8(0));

        u8 st = 0;
        st |= STATUS_ACKNOWLEDGE;
        mmio_write(&c->device_status, st);

        st |= STATUS_DRIVER;
        mmio_write(&c->device_status, st);

        u64 offered = read_device_features(c);

        if ((offered & F_VERSION_1) == 0)
        {
            st |= STATUS_FAILED;
            mmio_write(&c->device_status, st);
            return false;
        }

        u64 negotiated = features_to_accept & offered;
        negotiated |= F_VERSION_1;

        write_driver_features(c, negotiated);

        st |= STATUS_FEATURES_OK;
        mmio_write(&c->device_status, st);

        u8 back = mmio_read(&c->device_status);
        if ((back & STATUS_FEATURES_OK) == 0)
        {
            st |= STATUS_FAILED;
            mmio_write(&c->device_status, st);
            return false;
        }

        st |= STATUS_DRIVER_OK;
        mmio_write(&c->device_status, st);
        return true;
    }
}
