#include "gicv2.hpp"

namespace
{
    static constexpr u64 GICD_BASE = 0x08000000;
    static constexpr u64 GICC_BASE = 0x08010000;

    inline void mmio_write32(u64 addr, u32 v)
    {
        *reinterpret_cast<volatile u32*>(addr) = v;
    }

    inline u32 mmio_read32(u64 addr)
    {
        return *reinterpret_cast<volatile u32*>(addr);
    }

    inline void mmio_write8(u64 addr, u8 v)
    {
        *reinterpret_cast<volatile u8*>(addr) = v;
    }

    static constexpr u64 GICD_CTLR      = GICD_BASE + 0x000;
    static constexpr u64 GICD_IGROUPR   = GICD_BASE + 0x080;
    static constexpr u64 GICD_ISENABLER = GICD_BASE + 0x100;
    static constexpr u64 GICD_IPRIORITY = GICD_BASE + 0x400;

    static constexpr u64 GICC_CTLR = GICC_BASE + 0x0000;
    static constexpr u64 GICC_PMR  = GICC_BASE + 0x0004;
    static constexpr u64 GICC_BPR  = GICC_BASE + 0x0008;
    static constexpr u64 GICC_IAR  = GICC_BASE + 0x000C;
    static constexpr u64 GICC_EOIR = GICC_BASE + 0x0010;
}

namespace gicv2
{
    void init()
    {
        mmio_write32(GICD_CTLR, 0);
        mmio_write32(GICC_CTLR, 0);

        mmio_write32(GICC_PMR, 0xFF);
        mmio_write32(GICC_BPR, 0);

        mmio_write32(GICD_CTLR, 0x3);
        mmio_write32(GICC_CTLR, 0x3);
    }

    void enable_int(u32 intid)
    {
        u32 reg = intid / 32;
        u32 bit = intid % 32;

        u32 grp = mmio_read32(GICD_IGROUPR + (reg * 4));
        grp |= (1u << bit);
        mmio_write32(GICD_IGROUPR + (reg * 4), grp);

        mmio_write32(GICD_ISENABLER + (reg * 4), (1u << bit));

        mmio_write8(GICD_IPRIORITY + intid, 0x00);
    }

    u32 ack()
    {
        return mmio_read32(GICC_IAR);
    }

    void eoi(u32 iar)
    {
        mmio_write32(GICC_EOIR, iar);
    }
}
