/*
  draw.cpp - software rasterizer writing directly to the vgpu framebuffer
  fill_rect, draw_hline, draw_vline, draw_rect, draw_char, draw_text, blit, blit_alpha
  pixel format is bgra (b8g8r8x8)
*/
#include "kernel/gfx/draw.hpp"
#include "kernel/drivers/virtio/gpu.hpp"
#include <stdint.h>

namespace gfx {

static inline uint32_t* fb()         { return vgpu::framebuffer(); }
static inline uint32_t  scr_w()      { return vgpu::width();       }
static inline uint32_t  scr_h()      { return vgpu::height();      }

static inline uint32_t clamp(uint32_t v, uint32_t hi) {
    return (v < hi) ? v : hi;
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    fb()[y * scr_w() + x] = color;
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t fw = scr_w(), fh = scr_h();
    if (x >= fw || y >= fh || !w || !h) return;
    uint32_t x2 = clamp(x + w, fw);
    uint32_t y2 = clamp(y + h, fh);
    uint32_t* row = fb() + y * fw + x;
    uint32_t  rw  = x2 - x;
    for (uint32_t j = y; j < y2; ++j, row += fw) {
        for (uint32_t i = 0; i < rw; ++i)
            row[i] = color;
    }
}

void draw_hline(uint32_t x, uint32_t y, uint32_t len, uint32_t color) {
    fill_rect(x, y, len, 1, color);
}

void draw_vline(uint32_t x, uint32_t y, uint32_t len, uint32_t color) {
    fill_rect(x, y, 1, len, color);
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!w || !h) return;
    draw_hline(x,         y,         w,     color);
    draw_hline(x,         y + h - 1, w,     color);
    draw_vline(x,         y,         h,     color);
    draw_vline(x + w - 1, y,         h,     color);
}

void blit(const uint32_t* src,
          uint32_t dst_x, uint32_t dst_y,
          uint32_t w, uint32_t h) {
    blit_stride(src, w, dst_x, dst_y, w, h);
}

void blit_stride(const uint32_t* src, uint32_t src_stride_px,
                 uint32_t dst_x, uint32_t dst_y,
                 uint32_t w, uint32_t h) {
    uint32_t fw = scr_w(), fh = scr_h();
    if (dst_x >= fw || dst_y >= fh || !w || !h) return;
    uint32_t copy_w = ((dst_x + w) <= fw) ? w : (fw - dst_x);
    uint32_t copy_h = ((dst_y + h) <= fh) ? h : (fh - dst_y);
    uint32_t* dst_row = fb() + dst_y * fw + dst_x;
    for (uint32_t j = 0; j < copy_h; ++j) {
        const uint32_t* sr = src + j * src_stride_px;
        uint32_t*       dr = dst_row + j * fw;
        for (uint32_t i = 0; i < copy_w; ++i)
            dr[i] = sr[i];
    }
}

void blit_alpha(const uint32_t* src, uint32_t bg_color,
                uint32_t dst_x, uint32_t dst_y,
                uint32_t w, uint32_t h) {
    uint32_t fw = scr_w(), fh = scr_h();
    if (dst_x >= fw || dst_y >= fh || !w || !h) return;
    uint32_t copy_w = ((dst_x + w) <= fw) ? w : (fw - dst_x);
    uint32_t copy_h = ((dst_y + h) <= fh) ? h : (fh - dst_y);
    uint32_t bg_b = (bg_color      ) & 0xFFu;
    uint32_t bg_g = (bg_color >>  8) & 0xFFu;
    uint32_t bg_r = (bg_color >> 16) & 0xFFu;
    uint32_t* dst_row = fb() + dst_y * fw + dst_x;
    for (uint32_t j = 0; j < copy_h; ++j) {
        const uint32_t* sr = src + j * w;
        uint32_t*       dr = dst_row + j * fw;
        for (uint32_t i = 0; i < copy_w; ++i) {
            uint32_t p = sr[i];
            uint32_t a = (p >> 24) & 0xFFu;
            if (a == 0u) {
                dr[i] = bg_color;
            } else if (a == 255u) {
                dr[i] = p & 0x00FFFFFFu;
            } else {
                uint32_t ia = 255u - a;
                uint32_t ob = ((( p        & 0xFFu) * a + bg_b * ia + 127u) >> 8u);
                uint32_t og = ((((p >>  8) & 0xFFu) * a + bg_g * ia + 127u) >> 8u);
                uint32_t or_ = ((((p >> 16) & 0xFFu) * a + bg_r * ia + 127u) >> 8u);
                dr[i] = ob | (og << 8u) | (or_ << 16u);
            }
        }
    }
}

}
