#include "kernel/gui/terminal.hpp"
#include "kernel/gui/text.hpp"
#include "kernel/gui/font8x8.hpp"

#include "kernel/mm/heap/kheap.hpp"
#include "kernel/core/panic.hpp"

namespace gui_term
{
    static void clear_line(Terminal& t, u32 y)
    {
        for (u32 x = 0; x < t.cols; x++)
        {
            t.buf[y * t.cols + x] = ' ';
        }
    }

    static void scroll_up(Terminal& t)
    {
        for (u32 y = 1; y < t.rows; y++)
        {
            for (u32 x = 0; x < t.cols; x++)
            {
                t.buf[(y - 1) * t.cols + x] = t.buf[y * t.cols + x];
            }
        }

        clear_line(t, t.rows - 1);
    }

    void init(Terminal& t, u32 cols, u32 rows)
    {
        t.cols = cols;
        t.rows = rows;
        t.cur_x = 0;
        t.cur_y = 0;

        usize bytes = (usize)(cols * rows);
        t.buf = (char*)kheap::kmalloc(bytes);

        if (!t.buf)
        {
            panic("gui_term: kmalloc failed");
        }

        for (usize i = 0; i < bytes; i++)
        {
            t.buf[i] = ' ';
        }
    }

    void putc(Terminal& t, char c)
    {
        if (c == '\r')
        {
            t.cur_x = 0;
            return;
        }

        if (c == '\n')
        {
            t.cur_x = 0;
            t.cur_y++;

            if (t.cur_y >= t.rows)
            {
                scroll_up(t);
                t.cur_y = t.rows - 1;
            }
            return;
        }

        if (c == '\b')
        {
            if (t.cur_x > 0)
            {
                t.cur_x--;
                t.buf[t.cur_y * t.cols + t.cur_x] = ' ';
            }
            return;
        }

        if (c == '\t')
        {
            u32 next = (t.cur_x + 4) & ~3u;
            while (t.cur_x < next)
            {
                putc(t, ' ');
            }
            return;
        }

        if ((u8)c < 0x20)
        {
            return;
        }

        t.buf[t.cur_y * t.cols + t.cur_x] = c;
        t.cur_x++;

        if (t.cur_x >= t.cols)
        {
            t.cur_x = 0;
            t.cur_y++;

            if (t.cur_y >= t.rows)
            {
                scroll_up(t);
                t.cur_y = t.rows - 1;
            }
        }
    }

    void puts(Terminal& t, const char* s)
    {
        while (*s)
        {
            putc(t, *s++);
        }
    }

    void render(const Terminal& t, u32* fb, u32 stride_words, u32 x_px, u32 y_px, u32 fg, u32 bg)
    {
        for (u32 y = 0; y < t.rows; y++)
        {
            for (u32 x = 0; x < t.cols; x++)
            {
                char c = t.buf[y * t.cols + x];

                gui_text::draw_char(
                    fb,
                    stride_words,
                    x_px + x * font8x8::GLYPH_W,
                    y_px + y * font8x8::GLYPH_H,
                    c,
                    fg,
                    bg
                );
            }
        }

        // cursor underscore
        u32 cx = x_px + t.cur_x * font8x8::GLYPH_W;
        u32 cy = y_px + t.cur_y * font8x8::GLYPH_H;
        gui_text::draw_char(fb, stride_words, cx, cy, '_', fg, bg);
    }
}
