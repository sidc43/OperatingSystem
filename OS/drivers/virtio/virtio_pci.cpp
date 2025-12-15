#include "drivers/virtio/virtio_pci.hpp"

#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/assert.hpp"

#include "kernel/mm/vm/vm.hpp"

namespace
{
    struct __attribute__((packed)) PciCapHdr
    {
        u8 cap_vndr;
        u8 cap_next;
        u8 cap_len;
    };

    struct __attribute__((packed)) VirtioPciCap
    {
        u8  cap_vndr;    // 0x09
        u8  cap_next;
        u8  cap_len;
        u8  cfg_type;    // VIRTIO_PCI_CAP_*
        u8  bar;
        u8  padding[3];
        u32 offset;
        u32 length;
    };

    // ---- ECAM raw MMIO config access (no pci::read/write helpers needed) ----

    static inline u64 ecam_addr(const pci::Ecam& ecam, pci::Bdf bdf, u16 reg_off)
    {
        u64 off =
            ((u64)bdf.bus << 20) |
            ((u64)bdf.dev << 15) |
            ((u64)bdf.fun << 12) |
            ((u64)reg_off & 0x0FFFu);

        return ecam.base + off;
    }

    static inline u32 mmio_read32(u64 addr)
    {
        return *(volatile u32*)(uintptr_t)addr;
    }

    static inline void mmio_write32(u64 addr, u32 v)
    {
        *(volatile u32*)(uintptr_t)addr = v;
    }

    static inline u32 cfg_read32(const pci::Ecam& ecam, pci::Bdf bdf, u16 off)
    {
        return mmio_read32(ecam_addr(ecam, bdf, off));
    }

    static inline u16 cfg_read16(const pci::Ecam& ecam, pci::Bdf bdf, u16 off)
    {
        u64 a = ecam_addr(ecam, bdf, (u16)(off & ~3u));
        u32 v = mmio_read32(a);
        u32 sh = (u32)((off & 2u) * 8u);
        return (u16)((v >> sh) & 0xFFFFu);
    }

    static inline u8 cfg_read8(const pci::Ecam& ecam, pci::Bdf bdf, u16 off)
    {
        u64 a = ecam_addr(ecam, bdf, (u16)(off & ~3u));
        u32 v = mmio_read32(a);
        u32 sh = (u32)((off & 3u) * 8u);
        return (u8)((v >> sh) & 0xFFu);
    }

    static inline void cfg_write32(const pci::Ecam& ecam, pci::Bdf bdf, u16 off, u32 val)
    {
        mmio_write32(ecam_addr(ecam, bdf, off), val);
    }

    static inline void cfg_write16(const pci::Ecam& ecam, pci::Bdf bdf, u16 off, u16 val)
    {
        u64 a = ecam_addr(ecam, bdf, (u16)(off & ~3u));
        u32 v = mmio_read32(a);
        u32 sh = (u32)((off & 2u) * 8u);
        v &= ~(0xFFFFu << sh);
        v |= ((u32)val << sh);
        mmio_write32(a, v);
    }

    static inline bool present(const pci::Ecam& ecam, pci::Bdf bdf)
    {
        return (cfg_read16(ecam, bdf, 0x00) != 0xFFFFu);
    }

    // ---- BAR helpers ----

    static void enable_mem_busmaster(const pci::Ecam& ecam, pci::Bdf bdf)
    {
        u16 cmd = cfg_read16(ecam, bdf, 0x04);
        cmd |= (1u << 1); // MEM
        cmd |= (1u << 2); // BUS MASTER
        cfg_write16(ecam, bdf, 0x04, cmd);
    }

    static bool bar_is_mem(u32 bar_lo) { return ((bar_lo & 0x1u) == 0u); }
    static bool bar_is_64(u32 bar_lo)  { return ((bar_lo & 0x6u) == 0x4u); }

    static u64 read_bar_addr(const pci::Ecam& ecam, pci::Bdf bdf, u8 bar_index)
    {
        u16 off = (u16)(0x10 + (bar_index * 4));
        u32 lo = cfg_read32(ecam, bdf, off);

        if (!bar_is_mem(lo)) return 0;

        u64 addr = (u64)(lo & ~0xFull);
        if (bar_is_64(lo))
        {
            u32 hi = cfg_read32(ecam, bdf, (u16)(off + 4));
            addr |= ((u64)hi << 32);
        }
        return addr;
    }

    static void write_bar_addr(const pci::Ecam& ecam, pci::Bdf bdf, u8 bar_index, u64 addr)
    {
        u16 off = (u16)(0x10 + (bar_index * 4));
        u32 lo_old = cfg_read32(ecam, bdf, off);
        if (!bar_is_mem(lo_old)) return;

        u32 lo = (u32)(addr & 0xFFFF'FFFFull);
        lo = (lo & ~0xFull) | (lo_old & 0xFull);
        cfg_write32(ecam, bdf, off, lo);

        if (bar_is_64(lo_old))
        {
            u32 hi = (u32)(addr >> 32);
            cfg_write32(ecam, bdf, (u16)(off + 4), hi);
        }
    }

    static u64 bar_size_bytes(const pci::Ecam& ecam, pci::Bdf bdf, u8 bar_index)
    {
        u16 off = (u16)(0x10 + (bar_index * 4));
        u32 lo_old = cfg_read32(ecam, bdf, off);
        if (!bar_is_mem(lo_old)) return 0;

        bool is64 = bar_is_64(lo_old);
        u32 hi_old = is64 ? cfg_read32(ecam, bdf, (u16)(off + 4)) : 0;

        cfg_write32(ecam, bdf, off, 0xFFFF'FFFFu);
        u32 lo_mask = cfg_read32(ecam, bdf, off);

        u32 hi_mask = 0;
        if (is64)
        {
            cfg_write32(ecam, bdf, (u16)(off + 4), 0xFFFF'FFFFu);
            hi_mask = cfg_read32(ecam, bdf, (u16)(off + 4));
        }

        cfg_write32(ecam, bdf, off, lo_old);
        if (is64) cfg_write32(ecam, bdf, (u16)(off + 4), hi_old);

        u64 mask = (u64)(lo_mask & ~0xFull);
        if (is64) mask |= ((u64)hi_mask << 32);

        return (~mask) + 1;
    }

    // ---- Virtio caps ----

    static bool parse_virtio_caps(const pci::Ecam& ecam, pci::Bdf bdf, virtio_pci::Caps& out)
    {
        u8 cap_ptr = cfg_read8(ecam, bdf, 0x34);
        if (cap_ptr == 0) return false;

        bool have_common = false;
        bool have_notify = false;

        while (cap_ptr != 0)
        {
            PciCapHdr h {};
            h.cap_vndr = cfg_read8(ecam, bdf, (u16)(cap_ptr + 0));
            h.cap_next = cfg_read8(ecam, bdf, (u16)(cap_ptr + 1));
            h.cap_len  = cfg_read8(ecam, bdf, (u16)(cap_ptr + 2));

            if (h.cap_vndr == 0x09 && h.cap_len >= sizeof(VirtioPciCap))
            {
                VirtioPciCap vc {};
                vc.cfg_type = cfg_read8(ecam, bdf, (u16)(cap_ptr + 3));
                vc.bar      = cfg_read8(ecam, bdf, (u16)(cap_ptr + 4));
                vc.offset   = cfg_read32(ecam, bdf, (u16)(cap_ptr + 8));
                vc.length   = cfg_read32(ecam, bdf, (u16)(cap_ptr + 12));

                if (vc.cfg_type == virtio_pci::VIRTIO_PCI_CAP_COMMON_CFG)
                {
                    out.common_bar = vc.bar;
                    out.common_off = vc.offset;
                    out.common_len = vc.length;
                    have_common = true;
                }
                else if (vc.cfg_type == virtio_pci::VIRTIO_PCI_CAP_NOTIFY_CFG)
                {
                    out.notify_bar = vc.bar;
                    out.notify_off = vc.offset;
                    out.notify_len = vc.length;
                    out.notify_off_multiplier = cfg_read32(ecam, bdf, (u16)(cap_ptr + 16));
                    have_notify = true;
                }
                else if (vc.cfg_type == virtio_pci::VIRTIO_PCI_CAP_ISR_CFG)
                {
                    out.isr_bar = vc.bar;
                    out.isr_off = vc.offset;
                    out.isr_len = vc.length;
                }
                else if (vc.cfg_type == virtio_pci::VIRTIO_PCI_CAP_DEVICE_CFG)
                {
                    out.dev_bar = vc.bar;
                    out.dev_off = vc.offset;
                    out.dev_len = vc.length;
                }
            }

            cap_ptr = h.cap_next;
        }

        return have_common && have_notify;
    }

    // ---- Addressing strategy ----
    //
    // BAR PHYS addresses live in PCI memory space (QEMU typically 0x1000_0000...)
    // Mapping those with VA==PA can collide with existing mappings -> your map_range fails.
    // So we map BAR PA into a dedicated HIGH VA window and use that VA for pointers.
    //
    static u64 g_bar_pa_next = 0x0000000010000000ull;
    static u64 g_bar_va_next = 0x0000005000000000ull;

    static u64 align4k(u64 x) { return (x + 0xFFFu) & ~0xFFFu; }

    static u64 alloc_bar_pa(u64 size)
    {
        u64 s = align4k(size);
        u64 base = g_bar_pa_next;
        g_bar_pa_next += s;
        return base;
    }

    static u64 alloc_bar_va(u64 size)
    {
        u64 s = align4k(size);
        u64 base = g_bar_va_next;
        g_bar_va_next += s;
        return base;
    }

    static bool map_bar_pa_to_va(u64 va, u64 pa, u64 size)
    {
        size = align4k(size);
        return vm::map_range(va, pa, size, vm::DEVICE | vm::NOEXEC);
    }

    static bool build_transport_internal(const pci::Ecam& ecam, pci::Bdf bdf, virtio::PciTransport& out, bool verbose)
    {
        u16 vid = cfg_read16(ecam, bdf, 0x00);
        if (vid != 0x1AF4) return false;

        virtio_pci::Caps caps {};
        if (!parse_virtio_caps(ecam, bdf, caps)) return false;

        pci::enable_mem_busmaster(ecam, bdf);

        u64 bar_pa[6] {};
        u64 bar_va[6] {};
        u64 bar_sz[6] {};
        for (int i = 0; i < 6; i++)
        {
            bar_pa[i] = 0;
            bar_va[i] = 0;
            bar_sz[i] = 0;
        }

        u8 needed[2] = { caps.common_bar, caps.notify_bar };

        for (int i = 0; i < 2; i++)
        {
            u8 bar = needed[i];
            if (bar >= 6) return false;
            if (bar_pa[bar] != 0) continue;

            u64 size = bar_size_bytes(ecam, bdf, bar);
            if (size == 0) size = 0x4000;

            // Assign PHYS BAR address (what device decodes)
            u64 pa = read_bar_addr(ecam, bdf, bar);
            if (pa == 0)
            {
                pa = alloc_bar_pa(size);
                write_bar_addr(ecam, bdf, bar, pa);
            }

            // Map to HIGH VA (what kernel uses)
            u64 va = alloc_bar_va(size);

            if (verbose)
            {
                kprint::puts("virtio: BAR");
                kprint::dec_u64(bar);
                kprint::puts(" assigned pa=");
                kprint::hex_u64(pa);
                kprint::puts(" size=");
                kprint::hex_u64(size);
                kprint::puts(" -> va=");
                kprint::hex_u64(va);
                kprint::puts("\n");
            }

            if (!map_bar_pa_to_va(va, pa, size))
            {
                if (verbose)
                {
                    kprint::puts("virtio: mmio map failed for BAR\n");
                }
                return false;
            }

            bar_pa[bar] = pa;
            bar_va[bar] = va;
            bar_sz[bar] = size;
        }

        u64 common_va = bar_va[caps.common_bar] + caps.common_off;
        u64 notify_va = bar_va[caps.notify_bar] + caps.notify_off;

        out.common = (volatile virtio::PciCommonCfg*)(uintptr_t)common_va;
        out.notify_base = (volatile u8*)(uintptr_t)notify_va;
        out.notify_mult = caps.notify_off_multiplier;

        if (verbose)
        {
            kprint::puts("virtio: transport @ ");
            kprint::dec_u64(bdf.bus);
            kprint::puts(":");
            kprint::dec_u64(bdf.dev);
            kprint::puts(".");
            kprint::dec_u64(bdf.fun);
            kprint::puts("\n  common=");
            kprint::hex_u64((u64)(uintptr_t)out.common);
            kprint::puts(" notify=");
            kprint::hex_u64((u64)(uintptr_t)out.notify_base);
            kprint::puts(" mult=");
            kprint::hex_u64(out.notify_mult);
            kprint::puts("\n");
        }

        return true;
    }
}

namespace virtio_pci
{
    void scan_and_print(const pci::Ecam& ecam)
    {
        kprint::puts("virtio: scan start\n");

        for (u16 bus = ecam.bus_start; bus <= ecam.bus_end; bus++)
        {
            for (u16 dev = 0; dev < 32; dev++)
            {
                for (u16 fun = 0; fun < 8; fun++)
                {
                    pci::Bdf bdf { (u8)bus, (u8)dev, (u8)fun };

                    if (!pci::present(ecam, bdf))
                    {
                        if (fun == 0) break;
                        continue;
                    }

                    u16 vid = cfg_read16(ecam, bdf, 0x00);
                    if (vid == 0x1AF4)
                    {
                        u16 did = cfg_read16(ecam, bdf, 0x02);
                        kprint::puts("virtio: ");
                        kprint::dec_u64(bdf.bus);
                        kprint::puts(":");
                        kprint::dec_u64(bdf.dev);
                        kprint::puts(".");
                        kprint::dec_u64(bdf.fun);
                        kprint::puts(" pci_did=");
                        kprint::hex_u64(did);
                        kprint::puts("\n");
                    }
                }
            }
        }

        kprint::puts("virtio: scan done\n");
    }

    bool build_transport(const pci::Ecam& ecam, pci::Bdf bdf, virtio::PciTransport& out)
    {
        if (!build_transport_internal(ecam, bdf, out, true))
        {
            return false;
        }
        return true;
    }

    bool find_transport_by_pci_did(const pci::Ecam& ecam, u16 target_pci_did, virtio::PciTransport& out)
    {
        for (u16 bus = ecam.bus_start; bus <= ecam.bus_end; bus++)
        {
            for (u16 dev = 0; dev < 32; dev++)
            {
                for (u16 fun = 0; fun < 8; fun++)
                {
                    pci::Bdf bdf { (u8)bus, (u8)dev, (u8)fun };

                    if (!pci::present(ecam, bdf))
                    {
                        if (fun == 0) break;
                        continue;
                    }

                    u16 vid = cfg_read16(ecam, bdf, 0x00);
                    u16 did = cfg_read16(ecam, bdf, 0x02);

                    if (vid == 0x1AF4 && did == target_pci_did)
                    {
                        if (build_transport_internal(ecam, bdf, out, true))
                        {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }
}
