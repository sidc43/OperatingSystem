#include "drivers/virtio/input/virtio_input.hpp"

#include "kernel/core/panic.hpp"
#include "kernel/mm/phys/page_alloc.hpp"

namespace virtio_input
{
    static inline void virtio_status_init(volatile virtio::PciCommonCfg* c)
    {
        // VirtIO PCI modern status bits:
        // 1 ACKNOWLEDGE, 2 DRIVER, 4 DRIVER_OK, 8 FEATURES_OK, 0x80 FAILED
        c->device_status = 0;
        c->device_status = 1;           // ACKNOWLEDGE
        c->device_status |= 2;          // DRIVER

        // Negotiate zero features for now (safe baseline)
        c->driver_feature_select = 0;
        c->driver_feature = 0;
        c->driver_feature_select = 1;
        c->driver_feature = 0;

        c->device_status |= 8;          // FEATURES_OK

        // Device may clear FEATURES_OK if it didn't like our features
        if ((c->device_status & 8) == 0)
        {
            panic("virtio-input: FEATURES_OK rejected");
        }

        c->device_status |= 4;          // DRIVER_OK
    }

    bool Device::init(const virtio::PciTransport& t)
    {
        t_ = t;

        if (!t_.common)
        {
            return false;
        }

        virtio_status_init(t_.common);

        if (!q_.init(t_, 0))
        {
            return false;
        }

        page_ = phys::alloc_pages(1);
        if (!page_)
        {
            panic("virtio-input: alloc page failed");
        }

        ev_ = (Event*)page_;

        for (u16 i = 0; i < 256; i++)
        {
            desc_to_slot_[i] = 0;
            desc_valid_[i] = false;
        }

        for (u16 s = 0; s < SLOTS; s++)
        {
            post_slot(s);
        }

        ready_ = true;
        return true;
    }

    void Device::post_slot(u16 slot)
    {
        Event* e = &ev_[slot];
        e->type = 0;
        e->code = 0;
        e->value = 0;

        u16 d = q_.submit_write_only((void*)e, sizeof(Event));

        if (d < 256)
        {
            desc_to_slot_[d] = slot;
            desc_valid_[d] = true;
        }
    }

    bool Device::poll(Event& out)
    {
        if (!ready_)
        {
            return false;
        }

        u16 id = 0;
        u32 len = 0;

        if (!q_.wait_used(&id, &len))
        {
            return false;
        }

        if (id >= 256 || !desc_valid_[id])
        {
            panic("virtio-input: bad used id");
        }

        u16 slot = desc_to_slot_[id];
        desc_valid_[id] = false;

        out = ev_[slot];

        post_slot(slot);
        return true;
    }
}
