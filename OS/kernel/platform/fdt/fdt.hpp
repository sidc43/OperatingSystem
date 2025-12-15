#pragma once
#include "types.hpp"

namespace fdt
{
    struct PciHost
    {
        u64 ecam_base;
        u64 ecam_size;
        u32 bus_start;
        u32 bus_end;
    };

    bool parse_pci_host_ecam_generic(const void* dtb, PciHost& out);

    bool is_valid(const void* dtb);

    u32 magic(const void* dtb);
    void debug_print_header(const void* dtb);
}
