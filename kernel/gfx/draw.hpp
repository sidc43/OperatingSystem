/*
  draw.hpp - software rasterizer interface
  all coordinates are pixel-space, top-left origin
  color format: bgra b8g8r8x8 - use gfx::rgb(r,g,b) to build values
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace gfx {

static constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

static constexpr uint32_t BLACK   = 0x00000000u;
static constexpr uint32_t WHITE   = 0x00FFFFFFu;
static constexpr uint32_t RED     = 0x00FF0000u;
static constexpr uint32_t GREEN   = 0x0000FF00u;
static constexpr uint32_t BLUE    = 0x000000FFu;
static constexpr uint32_t GRAY    = 0x00808080u;
static constexpr uint32_t DARKGRAY= 0x00303030u;

void draw_pixel(uint32_t x, uint32_t y, uint32_t color);

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

void draw_hline(uint32_t x, uint32_t y, uint32_t len, uint32_t color);

void draw_vline(uint32_t x, uint32_t y, uint32_t len, uint32_t color);

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

void blit(const uint32_t* src, uint32_t dst_x, uint32_t dst_y,
          uint32_t w, uint32_t h);

void blit_alpha(const uint32_t* src, uint32_t bg_color,
                uint32_t dst_x, uint32_t dst_y,
                uint32_t w, uint32_t h);

void blit_stride(const uint32_t* src, uint32_t src_stride_px,
                 uint32_t dst_x, uint32_t dst_y,
                 uint32_t w, uint32_t h);

}
