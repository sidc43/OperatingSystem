#pragma once
#include "types.hpp"

namespace gui_term
{
    struct Terminal
    {
        u32 cols {};
        u32 rows {};

        u32 cur_x {};
        u32 cur_y {};

        char* buf {}; // rows * cols
    };

    void init(Terminal& t, u32 cols, u32 rows);
    void putc(Terminal& t, char c);
    void puts(Terminal& t, const char* s);

    void render(const Terminal& t, u32* fb, u32 stride_words, u32 x_px, u32 y_px, u32 fg, u32 bg);
}
