#pragma once
#include "types.hpp"
#include "drivers/virtio/virtio_core.hpp"

namespace virtio
{
    constexpr u16 VQ_DESC_F_NEXT  = 1u;
    constexpr u16 VQ_DESC_F_WRITE = 2u;

    struct __attribute__((packed)) VqDesc
    {
        u64 addr;
        u32 len;
        u16 flags;
        u16 next;
    };

    struct __attribute__((packed)) VqAvail
    {
        u16 flags;
        u16 idx;
        u16 ring[1];
    };

    struct __attribute__((packed)) VqUsedElem
    {
        u32 id;
        u32 len;
    };

    struct __attribute__((packed)) VqUsed
    {
        u16 flags;
        u16 idx;
        VqUsedElem ring[1];
    };

    struct PciTransport
    {
        volatile PciCommonCfg* common;
        volatile u8* notify_base;
        u32 notify_mult;
    };

    class VirtQueue
    {
    public:
        bool init(const PciTransport& t, u16 queue_index);

        u16 submit_2(const void* in_buf, u32 in_len, void* out_buf, u32 out_len);
        u16 submit_write_only(void* out_buf, u32 out_len);

        bool wait_used(u16* out_id, u32* out_len);

    private:
        u16 alloc_desc();
        void free_desc(u16 idx);

        void push_avail(u16 head);
        void notify();

    private:
        PciTransport t_ {};
        u16 q_index_ {};
        u16 q_size_ {};

        VqDesc*  desc_ {};
        VqAvail* avail_ {};
        VqUsed*  used_ {};

        volatile u16* notify_reg_ {};

        u16 last_used_ {};
        bool free_[256] {};
    };
}
