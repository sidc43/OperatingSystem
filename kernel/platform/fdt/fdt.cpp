#include "kernel/platform/fdt/fdt.hpp"
#include "kernel/core/print.hpp"

namespace
{
    static u32 be32(const void* p)
    {
        const u8* b = (const u8*)p;
        return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
    }

    static u64 be64_from_cells(const void* p)
    {
        const u8* b = (const u8*)p;
        u32 hi = be32(b);
        u32 lo = be32(b + 4);
        return (u64(hi) << 32) | u64(lo);
    }

    static bool streq(const char* a, const char* b)
    {
        while (*a && *b)
        {
            if (*a != *b) return false;
            a++;
            b++;
        }
        return *a == *b;
    }

    static bool compat_contains(const void* data, u32 len, const char* needle)
    {
        const char* s = (const char*)data;
        u32 i = 0;

        while (i < len)
        {
            if (streq(s, needle))
            {
                return true;
            }

            while (i < len && s[0] != '\0')
            {
                s++;
                i++;
            }

            if (i < len && s[0] == '\0')
            {
                s++;
                i++;
            }
        }

        return false;
    }

    static u32 align4(u32 x)
    {
        return (x + 3) & ~3u;
    }

    static constexpr u32 FDT_MAGIC = 0xD00DFEED;
    static constexpr u32 FDT_BEGIN_NODE = 1;
    static constexpr u32 FDT_END_NODE   = 2;
    static constexpr u32 FDT_PROP       = 3;
    static constexpr u32 FDT_NOP        = 4;
    static constexpr u32 FDT_END        = 9;

    struct FdtHeader
    {
        u32 magic;
        u32 totalsize;
        u32 off_dt_struct;
        u32 off_dt_strings;
        u32 off_mem_rsvmap;
        u32 version;
        u32 last_comp_version;
        u32 boot_cpuid_phys;
        u32 size_dt_strings;
        u32 size_dt_struct;
    };
}

namespace fdt
{
    bool parse_pci_host_ecam_generic(const void* dtb, PciHost& out)
    {
        const FdtHeader* h = (const FdtHeader*)dtb;

        if (be32(&h->magic) != FDT_MAGIC)
        {
            return false;
        }

        u32 off_struct  = be32(&h->off_dt_struct);
        u32 off_strings = be32(&h->off_dt_strings);
        u32 size_struct = be32(&h->size_dt_struct);

        const u8* structp  = (const u8*)dtb + off_struct;
        const u8* strings  = (const u8*)dtb + off_strings;
        const u8* endp     = structp + size_struct;

        static constexpr int MAX_DEPTH = 32;

        bool is_pci[MAX_DEPTH];
        const void* reg_data[MAX_DEPTH];
        u32 reg_len[MAX_DEPTH];
        const void* bus_data[MAX_DEPTH];
        u32 bus_len[MAX_DEPTH];

        int depth = -1;

        const u8* p = structp;

        while (p < endp)
        {
            u32 token = be32(p);
            p += 4;

            if (token == FDT_BEGIN_NODE)
            {
                depth++;
                if (depth >= MAX_DEPTH)
                {
                    return false;
                }

                is_pci[depth] = false;
                reg_data[depth] = nullptr;
                reg_len[depth] = 0;
                bus_data[depth] = nullptr;
                bus_len[depth] = 0;

                while ((uintptr_t)p < (uintptr_t)endp && *p != 0) p++;
                p++;
                p = (const u8*)(((uintptr_t)p + 3) & ~3ull);
                continue;
            }

            if (token == FDT_END_NODE)
            {
                if (depth >= 0 && is_pci[depth] && reg_data[depth] != nullptr && reg_len[depth] >= 8)
                {
                    if (reg_len[depth] == 8)
                    {
                        out.ecam_base = (u64)be32(reg_data[depth]);
                        out.ecam_size = (u64)be32((const u8*)reg_data[depth] + 4);
                    }
                    else
                    {
                        out.ecam_base = be64_from_cells(reg_data[depth]);
                        out.ecam_size = be64_from_cells((const u8*)reg_data[depth] + 8);
                    }

                    out.bus_start = 0;
                    out.bus_end   = 255;

                    if (bus_data[depth] != nullptr && bus_len[depth] >= 8)
                    {
                        out.bus_start = be32(bus_data[depth]);
                        out.bus_end   = be32((const u8*)bus_data[depth] + 4);
                    }

                    return true;
                }

                depth--;
                continue;
            }

            if (token == FDT_PROP)
            {
                u32 len = be32(p); p += 4;
                u32 nameoff = be32(p); p += 4;

                const char* pname = (const char*)(strings + nameoff);
                const void* pdata = p;

                p += align4(len);

                if (depth < 0)
                {
                    continue;
                }

                if (streq(pname, "compatible"))
                {
                    if (compat_contains(pdata, len, "pci-host-ecam-generic") ||
                        compat_contains(pdata, len, "pcie-host-ecam-generic"))
                    {
                        is_pci[depth] = true;
                    }
                }
                else if (streq(pname, "device_type"))
                {
                    const char* s = (const char*)pdata;
                    if (len >= 3 && streq(s, "pci"))
                    {
                        is_pci[depth] = true;
                    }
                }
                else if (streq(pname, "reg"))
                {
                    reg_data[depth] = pdata;
                    reg_len[depth]  = len;
                }
                else if (streq(pname, "bus-range"))
                {
                    bus_data[depth] = pdata;
                    bus_len[depth]  = len;
                }

                continue;
            }

            if (token == FDT_NOP)
            {
                continue;
            }

            if (token == FDT_END)
            {
                break;
            }

            return false;
        }

        return false;
    }

    bool is_valid(const void* dtb)
    {
        const FdtHeader* h = (const FdtHeader*)dtb;
        return be32(&h->magic) == FDT_MAGIC;
    }

    u32 magic(const void* dtb)
    {
        return be32(dtb);
    }

    void debug_print_header(const void* dtb)
    {
        const FdtHeader* h = (const FdtHeader*)dtb;

        kprint::puts("fdt hdr: magic=");
        kprint::hex_u64(be32(&h->magic));
        kprint::puts(" totalsize=");
        kprint::hex_u64(be32(&h->totalsize));
        kprint::puts(" off_struct=");
        kprint::hex_u64(be32(&h->off_dt_struct));
        kprint::puts(" off_strings=");
        kprint::hex_u64(be32(&h->off_dt_strings));
        kprint::puts(" size_struct=");
        kprint::hex_u64(be32(&h->size_dt_struct));
        kprint::puts(" size_strings=");
        kprint::hex_u64(be32(&h->size_dt_strings));
        kprint::puts("\n");
    }
}
