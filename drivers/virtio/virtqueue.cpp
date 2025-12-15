#include "drivers/virtio/virtqueue.hpp"

#include "kernel/core/panic.hpp"
#include "kernel/mm/phys/page_alloc.hpp"

namespace virtio
{
    static constexpr u16 VIRTQ_MAX = 256;

    static inline void dmb_oshst()
    {
        asm volatile("dmb oshst" ::: "memory");
    }

    static inline void dmb_oshld()
    {
        asm volatile("dmb oshld" ::: "memory");
    }

    static inline void dsb_oshst()
    {
        asm volatile("dsb oshst" ::: "memory");
    }

    static inline u16 min_u16(u16 a, u16 b)
    {
        return a < b ? a : b;
    }

    static inline usize align_up(usize x, usize a)
    {
        return (x + a - 1) & ~(a - 1);
    }

    static inline void zero_bytes(void* p, usize n)
    {
        u8* b = (u8*)p;
        for (usize i = 0; i < n; i++)
        {
            b[i] = 0;
        }
    }

    bool VirtQueue::init(const PciTransport& t, u16 queue_index)
    {
        t_ = t;
        q_index_ = queue_index;

        if (!t_.common || !t_.notify_base)
        {
            return false;
        }

        t_.common->queue_select = q_index_;
        dsb_oshst();

        u16 sz = t_.common->queue_size;
        if (sz == 0)
        {
            return false;
        }

        q_size_ = min_u16(sz, VIRTQ_MAX);

        usize desc_bytes  = (usize)q_size_ * sizeof(VqDesc);
        usize avail_bytes = 4 + (usize)q_size_ * sizeof(u16) + sizeof(u16);
        usize used_bytes  = 4 + (usize)q_size_ * sizeof(VqUsedElem) + sizeof(u16);

        desc_bytes  = align_up(desc_bytes,  16);
        avail_bytes = align_up(avail_bytes, 16);
        used_bytes  = align_up(used_bytes,  16);

        void* desc_p  = phys::alloc_pages((usize)((desc_bytes  + 4095) / 4096));
        void* avail_p = phys::alloc_pages((usize)((avail_bytes + 4095) / 4096));
        void* used_p  = phys::alloc_pages((usize)((used_bytes  + 4095) / 4096));

        if (!desc_p || !avail_p || !used_p)
        {
            return false;
        }

        desc_  = (VqDesc*)desc_p;
        avail_ = (VqAvail*)avail_p;
        used_  = (VqUsed*)used_p;

        zero_bytes(desc_,  desc_bytes);
        zero_bytes(avail_, avail_bytes);
        zero_bytes(used_,  used_bytes);

        for (u16 i = 0; i < VIRTQ_MAX; i++)
        {
            free_[i] = false;
        }
        for (u16 i = 0; i < q_size_; i++)
        {
            free_[i] = true;
        }

        last_used_ = 0;

        t_.common->queue_select = q_index_;
        dsb_oshst();

        u64 desc_pa  = (u64)(uintptr_t)desc_;
        u64 avail_pa = (u64)(uintptr_t)avail_;
        u64 used_pa  = (u64)(uintptr_t)used_;

        t_.common->queue_desc_lo   = (u32)desc_pa;
        t_.common->queue_desc_hi   = (u32)(desc_pa >> 32);

        t_.common->queue_avail_lo  = (u32)avail_pa;
        t_.common->queue_avail_hi  = (u32)(avail_pa >> 32);

        t_.common->queue_used_lo   = (u32)used_pa;
        t_.common->queue_used_hi   = (u32)(used_pa >> 32);

        dsb_oshst();

        t_.common->queue_enable = 1;
        dsb_oshst();

        u16 notify_off = t_.common->queue_notify_off;
        notify_reg_ = (volatile u16*)(uintptr_t)(t_.notify_base + (u64)notify_off * (u64)t_.notify_mult);

        return true;
    }

    u16 VirtQueue::alloc_desc()
    {
        for (u16 i = 0; i < q_size_; i++)
        {
            if (free_[i])
            {
                free_[i] = false;
                return i;
            }
        }

        panic("virtqueue: out of descriptors");
        return 0;
    }

    void VirtQueue::free_desc(u16 idx)
    {
        if (idx < q_size_)
        {
            free_[idx] = true;
        }
    }

    void VirtQueue::push_avail(u16 head)
    {
        u16 idx = avail_->idx;
        avail_->ring[idx % q_size_] = head;

        dmb_oshst();
        avail_->idx = (u16)(idx + 1);
        dmb_oshst();
    }

    void VirtQueue::notify()
    {
        *notify_reg_ = q_index_;
        dsb_oshst();
    }

    u16 VirtQueue::submit_2(const void* in_buf, u32 in_len, void* out_buf, u32 out_len)
    {
        if (!in_buf || !out_buf)
        {
            panic("virtqueue: submit_2 null buffer");
        }

        u16 d0 = alloc_desc();
        u16 d1 = alloc_desc();

        desc_[d0].addr  = (u64)(uintptr_t)in_buf;
        desc_[d0].len   = in_len;
        desc_[d0].flags = (u16)(VQ_DESC_F_NEXT);
        desc_[d0].next  = d1;

        desc_[d1].addr  = (u64)(uintptr_t)out_buf;
        desc_[d1].len   = out_len;
        desc_[d1].flags = (u16)(VQ_DESC_F_WRITE);
        desc_[d1].next  = 0;

        dmb_oshst();

        push_avail(d0);
        notify();

        return d0;
    }

    u16 VirtQueue::submit_write_only(void* out_buf, u32 out_len)
    {
        if (!out_buf)
        {
            panic("virtqueue: submit_write_only null buffer");
        }

        u16 d0 = alloc_desc();

        desc_[d0].addr  = (u64)(uintptr_t)out_buf;
        desc_[d0].len   = out_len;
        desc_[d0].flags = (u16)(VQ_DESC_F_WRITE);
        desc_[d0].next  = 0;

        dmb_oshst();

        push_avail(d0);
        notify();

        return d0;
    }

    bool VirtQueue::wait_used(u16* out_id, u32* out_len)
    {
        dmb_oshld();
        u16 uidx = used_->idx;

        if (uidx == last_used_)
        {
            return false;
        }

        VqUsedElem e = used_->ring[last_used_ % q_size_];
        last_used_++;

        if (out_id)  *out_id  = (u16)e.id;
        if (out_len) *out_len = e.len;

        u16 head = (u16)e.id;
        if (head < q_size_)
        {
            u16 next = desc_[head].next;
            u16 flags = desc_[head].flags;

            free_desc(head);

            if ((flags & VQ_DESC_F_NEXT) && next < q_size_)
            {
                free_desc(next);
            }
        }

        return true;
    }
}
