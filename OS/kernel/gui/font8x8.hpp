#pragma once
#include "types.hpp"

namespace font8x8
{
    constexpr u32 GLYPH_W = 8;
    constexpr u32 GLYPH_H = 8;

    extern const u8 basic[96][8];

    inline const u8* glyph(char c)
    {
        // If your terminal buffer contains 0 (very common when memory isn’t cleared),
        // render it as a space instead of turning the whole screen into '?'.
        if (c == 0)
        {
            c = ' ';
        }

        if (c < 0x20 || c > 0x7F)
        {
            c = '?';
        }

        return basic[(u8)c - 0x20];
    }
}
