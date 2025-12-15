#pragma once
#include "types.hpp"

namespace pci
{
    struct Ecam
    {
        u64 base;
        u32 bus_start;
        u32 bus_end;
    };

    struct Bdf
    {
        u8 bus;
        u8 dev;
        u8 fun;
    };

    u32 read32(const Ecam& ecam, Bdf bdf, u32 off);
    u16 read16(const Ecam& ecam, Bdf bdf, u32 off);
    u8  read8 (const Ecam& ecam, Bdf bdf, u32 off);

    void write32(const Ecam& ecam, Bdf bdf, u32 off, u32 val);
    void write16(const Ecam& ecam, Bdf bdf, u32 off, u16 val);

    bool present(const Ecam& ecam, Bdf bdf);

    void scan_and_print(const Ecam& ecam);

    void enable_mem_busmaster(const Ecam& ecam, Bdf bdf);

    struct BarInfo
    {
        bool io;
        bool is64;
        u64  addr;
        u64  size;
        bool valid;
    };

    BarInfo probe_bar(const Ecam& ecam, Bdf bdf, u8 bar_index);
    void    write_bar_addr(const Ecam& ecam, Bdf bdf, u8 bar_index, u64 addr, bool is64);

    void scan_and_print(const Ecam& ecam);
}
