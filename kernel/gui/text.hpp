#pragma once
#include "types.hpp"

namespace gui_text
{
    void draw_char(u32* fb, u32 stride_words, u32 x, u32 y, char c, u32 fg, u32 bg);
    void draw_string(u32* fb, u32 stride_words, u32 x, u32 y, const char* s, u32 fg, u32 bg);
}
