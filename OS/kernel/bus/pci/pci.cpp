#include "kernel/bus/pci/pci.hpp"
#include "kernel/core/print.hpp"

namespace
{
    static inline volatile u32* ecam_ptr(u64 base, u32 bus, u32 dev, u32 fun, u32 off)
    {
        u64 addr = base
                 + (u64(bus) << 20)
                 + (u64(dev) << 15)
                 + (u64(fun) << 12)
                 + (u64(off) & 0xFFFull);

        return (volatile u32*)(uintptr_t)addr;
    }

    static u16 lo16(u32 x) { return (u16)(x & 0xFFFFu); }
    static u16 hi16(u32 x) { return (u16)((x >> 16) & 0xFFFFu); }
    static u8  b8(u32 x, u32 s) { return (u8)((x >> s) & 0xFFu); }
}

namespace pci
{
    u32 read32(const Ecam& ecam, Bdf bdf, u32 off)
    {
        return *ecam_ptr(ecam.base, bdf.bus, bdf.dev, bdf.fun, off);
    }

    u16 read16(const Ecam& ecam, Bdf bdf, u32 off)
    {
        u32 v = read32(ecam, bdf, off & ~3u);
        u32 s = (off & 2u) ? 16 : 0;
        return (u16)((v >> s) & 0xFFFFu);
    }

    u8 read8(const Ecam& ecam, Bdf bdf, u32 off)
    {
        u32 v = read32(ecam, bdf, off & ~3u);
        u32 s = (off & 3u) * 8u;
        return (u8)((v >> s) & 0xFFu);
    }

    void write32(const Ecam& ecam, Bdf bdf, u32 off, u32 val)
    {
        *ecam_ptr(ecam.base, bdf.bus, bdf.dev, bdf.fun, off) = val;
    }

    void write16(const Ecam& ecam, Bdf bdf, u32 off, u16 val)
    {
        u32 aligned = off & ~3u;
        u32 v = read32(ecam, bdf, aligned);

        if ((off & 2u) != 0)
        {
            v = (v & 0x0000FFFFu) | (u32(val) << 16);
        }
        else
        {
            v = (v & 0xFFFF0000u) | u32(val);
        }

        write32(ecam, bdf, aligned, v);
    }

    bool present(const Ecam& ecam, Bdf bdf)
    {
        u32 id = read32(ecam, bdf, 0x00);
        return lo16(id) != 0xFFFF;
    }

    void scan_and_print(const Ecam& ecam)
    {
        kprint::puts("pci: scan start\n");

        for (u32 bus = ecam.bus_start; bus <= ecam.bus_end; bus++)
        {
            for (u32 dev = 0; dev < 32; dev++)
            {
                for (u32 fun = 0; fun < 8; fun++)
                {
                    Bdf bdf;
                    bdf.bus = (u8)bus;
                    bdf.dev = (u8)dev;
                    bdf.fun = (u8)fun;

                    u32 id = read32(ecam, bdf, 0x00);
                    u16 vendor = lo16(id);

                    if (vendor == 0xFFFF)
                    {
                        if (fun == 0) break;
                        continue;
                    }

                    u16 device = hi16(id);

                    u32 classreg = read32(ecam, bdf, 0x08);
                    u8 class_code = b8(classreg, 24);
                    u8 subclass   = b8(classreg, 16);
                    u8 prog_if    = b8(classreg, 8);

                    kprint::puts("pci: ");
                    kprint::dec_u64(bus);
                    kprint::puts(":");
                    kprint::dec_u64(dev);
                    kprint::puts(".");
                    kprint::dec_u64(fun);
                    kprint::puts(" vid=");
                    kprint::hex_u64(vendor);
                    kprint::puts(" did=");
                    kprint::hex_u64(device);
                    kprint::puts(" class=");
                    kprint::hex_u64(class_code);
                    kprint::puts(":");
                    kprint::hex_u64(subclass);
                    kprint::puts(" if=");
                    kprint::hex_u64(prog_if);
                    kprint::puts("\n");
                }
            }
        }

        kprint::puts("pci: scan done\n");
    }

    void enable_mem_busmaster(const Ecam& ecam, Bdf bdf)
{
    u16 cmd = read16(ecam, bdf, 0x04);

    // bit 1 = MEM space enable, bit 2 = Bus Master enable
    cmd |= (1u << 1);
    cmd |= (1u << 2);

    write16(ecam, bdf, 0x04, cmd);
}

static inline u64 align_up_u64(u64 x, u64 a)
{
    return (x + a - 1) & ~(a - 1);
}

BarInfo probe_bar(const Ecam& ecam, Bdf bdf, u8 bar_index)
{
    BarInfo bi;
    bi.io = false;
    bi.is64 = false;
    bi.addr = 0;
    bi.size = 0;
    bi.valid = false;

    u32 off = 0x10 + 4u * bar_index;

    u32 orig_lo = read32(ecam, bdf, off);

    // I/O BAR
    if (orig_lo & 0x1u)
    {
        bi.io = true;
        return bi;
    }

    u32 type = (orig_lo >> 1) & 0x3u;
    bi.is64 = (type == 0x2u);

    u32 orig_hi = 0;
    if (bi.is64)
    {
        orig_hi = read32(ecam, bdf, off + 4);
    }

    // Size probing: write all 1s, read back mask
    write32(ecam, bdf, off, 0xFFFFFFFFu);
    u32 mask_lo = read32(ecam, bdf, off);

    u32 mask_hi = 0xFFFFFFFFu;
    if (bi.is64)
    {
        write32(ecam, bdf, off + 4, 0xFFFFFFFFu);
        mask_hi = read32(ecam, bdf, off + 4);
    }

    // Restore originals
    write32(ecam, bdf, off, orig_lo);
    if (bi.is64)
    {
        write32(ecam, bdf, off + 4, orig_hi);
    }

    u64 mask = (u64)(mask_lo & ~0xFull);
    if (bi.is64)
    {
        mask |= (u64(mask_hi) << 32);
    }

    if (mask == 0)
    {
        return bi;
    }

    u64 size = (~mask) + 1;

    u64 addr = (u64)(orig_lo & ~0xFull);
    if (bi.is64)
    {
        addr |= (u64(orig_hi) << 32);
    }

    bi.addr = addr;
    bi.size = size;
    bi.valid = (size != 0);
    return bi;
}

void write_bar_addr(const Ecam& ecam, Bdf bdf, u8 bar_index, u64 addr, bool is64)
{
    u32 off = 0x10 + 4u * bar_index;

    write32(ecam, bdf, off, (u32)(addr & 0xFFFFFFFFu));

    if (is64)
    {
        write32(ecam, bdf, off + 4, (u32)((addr >> 32) & 0xFFFFFFFFu));
    }
}

}
