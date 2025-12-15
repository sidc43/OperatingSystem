#pragma once
#include "types.hpp"
#include "drivers/virtio/virtqueue.hpp"

namespace virtio_input
{
    // Matches Linux input event layout conceptually:
    // type (u16), code (u16), value (signed 32-bit)
    struct __attribute__((packed)) Event
    {
        u16 type;
        u16 code;
        int value;
    };

    class Device
    {
    public:
        bool init(const virtio::PciTransport& t);
        bool poll(Event& out);

    private:
        void post_slot(u16 slot);

    private:
        virtio::PciTransport t_ {};
        virtio::VirtQueue q_ {};
        bool ready_ { false };

        void* page_ {};
        Event* ev_ {};

        static constexpr u16 SLOTS = 64;

        u16 desc_to_slot_[256] {};
        bool desc_valid_[256] {};
    };
}
