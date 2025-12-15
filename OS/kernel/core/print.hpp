#pragma once
#include "types.hpp"

namespace kprint 
{
    void putc(char c);
    void puts(const char* s);

    void hex_u64(u64 x);
    void dec_u64(u64 x);
}
