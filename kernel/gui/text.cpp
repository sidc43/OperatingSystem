#include "kernel/gui/text.hpp"
#include "kernel/gui/font8x8.hpp"

namespace gui_text
{
    void draw_char(u32* fb, u32 stride_words, u32 x, u32 y, char c, u32 fg, u32 bg)
    {
        const u8* g = font8x8::glyph(c);

        for (u32 row = 0; row < font8x8::GLYPH_H; row++)
        {
            u8 bits = g[row];
            u32* dst = fb + (y + row) * stride_words + x;

            for (u32 col = 0; col < font8x8::GLYPH_W; col++)
            {
                bool on = (bits & (1u << (7 - col))) != 0;
                dst[col] = on ? fg : bg;
            }
        }
    }

    void draw_string(u32* fb, u32 stride_words, u32 x, u32 y, const char* s, u32 fg, u32 bg)
    {
        u32 cx = x;
        while (*s)
        {
            draw_char(fb, stride_words, cx, y, *s, fg, bg);
            cx += font8x8::GLYPH_W;
            s++;
        }
    }
}
