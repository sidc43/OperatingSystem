/*
  font.hpp - 8x16 bitmap font interface
  FONT_W=8 FONT_H=16, draw_char() and draw_text() write into the vgpu framebuffer
*/
#pragma once
#include <stdint.h>

namespace gfx {

static constexpr uint32_t FONT_W       = 8;
static constexpr uint32_t FONT_H       = 16;
static constexpr uint32_t FONT_FIRST   = 0x20;
static constexpr uint32_t FONT_LAST    = 0x7E;

extern const uint8_t font8x16[];

void draw_char(uint32_t x, uint32_t y, char c,
               uint32_t fg = 0x00FFFFFFu, uint32_t bg = 0x00000000u);

void draw_text(uint32_t x, uint32_t y, const char* s,
               uint32_t fg = 0x00FFFFFFu, uint32_t bg = 0x00000000u);

uint32_t draw_text_ex(uint32_t x, uint32_t y, const char* s,
                      uint32_t fg, uint32_t bg);

void draw_char_into(uint32_t* fb, uint32_t fb_w,
                    uint32_t x, uint32_t y, char c,
                    uint32_t fg = 0x00FFFFFFu, uint32_t bg = 0x00000000u);

}
