/*
  fdt.cpp - minimal flattened device tree scanner
  qemu passes the dtb address in x0 at boot
  we only care about one thing: finding virtio-mmio node base addresses
  if no dtb or it looks bad, the caller falls back to probing fixed addresses
*/
#include "kernel/platform/fdt.hpp"
#include <stdint.h>
#include <stddef.h>

static constexpr uint32_t FDT_MAGIC      = 0xd00dfeedu;
static constexpr uint32_t FDT_BEGIN_NODE = 1u;
static constexpr uint32_t FDT_END_NODE   = 2u;
static constexpr uint32_t FDT_PROP       = 3u;
static constexpr uint32_t FDT_NOP        = 4u;
static constexpr uint32_t FDT_END        = 9u;

static inline uint32_t be32(const void* p) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
         | ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

struct FdtHeader {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

static inline const uint8_t* align4(const uint8_t* p) {
    return reinterpret_cast<const uint8_t*>((reinterpret_cast<uintptr_t>(p) + 3u) & ~3u);
}

static bool streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool compat_contains(const uint8_t* hay, uint32_t len, const char* needle) {
    const uint8_t* end = hay + len;
    while (hay < end) {
        if (streq(reinterpret_cast<const char*>(hay), needle)) return true;
        while (hay < end && *hay) ++hay;
        ++hay;
    }
    return false;
}

namespace fdt {

bool valid(const void* dtb) {
    if (!dtb) return false;
    return be32(dtb) == FDT_MAGIC;
}

int collect_virtio_mmio_regs(const void* dtb, uintptr_t* out, int max) {
    if (!valid(dtb)) return 0;
    if (max <= 0)    return 0;

    const uint8_t* base = static_cast<const uint8_t*>(dtb);

    auto hdr = [&](uint32_t off) { return be32(base + off); };
    uint32_t off_struct  = hdr(8);
    uint32_t off_strings = hdr(12);

    const uint8_t* strings = base + off_strings;
    const uint8_t* p       = base + off_struct;
    const uint8_t* p_end   = p + hdr(36);

    int found = 0;

    bool     in_virtio   = false;
    uint64_t node_reg    = 0;
    bool     have_reg    = false;

    int      node_depth  = 0;
    int      virtio_depth = -1;

    while (p < p_end) {
        p = align4(p);
        if (p >= p_end) break;

        uint32_t token = be32(p);
        p += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            node_depth++;

            while (*p) ++p;
            ++p;
            break;
        }
        case FDT_END_NODE: {
            if (in_virtio && node_depth == virtio_depth) {

                if (have_reg && found < max) {
                    out[found++] = (uintptr_t)node_reg;
                }
                in_virtio    = false;
                have_reg     = false;
                node_reg     = 0;
                virtio_depth = -1;
            }
            node_depth--;
            break;
        }
        case FDT_PROP: {
            uint32_t prop_len    = be32(p);     p += 4;
            uint32_t prop_nameoff= be32(p);     p += 4;
            const uint8_t* val   = p;
            p += prop_len;

            const char* prop_name =
                reinterpret_cast<const char*>(strings + prop_nameoff);

            if (streq(prop_name, "compatible")) {
                if (compat_contains(val, prop_len, "virtio,mmio")) {
                    in_virtio    = true;
                    virtio_depth = node_depth;
                }
            } else if (in_virtio && streq(prop_name, "reg")) {

                if (prop_len >= 16) {
                    uint64_t addr_hi = be32(val + 0);
                    uint64_t addr_lo = be32(val + 4);
                    node_reg = (addr_hi << 32) | addr_lo;
                    have_reg = true;
                } else if (prop_len >= 4) {

                    node_reg = be32(val);
                    have_reg = true;
                }
            }
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            goto done;
        default:

            goto done;
        }
    }
done:
    return found;
}

}
