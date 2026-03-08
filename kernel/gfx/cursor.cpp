/*
  cursor.cpp - 16x16 arrow cursor sprite with save-behind
  the save-behind pattern lets us erase the old sprite cheaply without a full redraw
  the sprite is alpha-blended over whatever is underneath it
*/
#include "kernel/gfx/cursor.hpp"
#include "kernel/drivers/virtio/gpu.hpp"
#include "arch/aarch64/regs.hpp"
#include <stdint.h>

static const uint16_t k_blk[16] = {
    0x8000,
    0xC000,
    0xA000,
    0x9000,
    0x8800,
    0x8400,
    0x8200,
    0x8100,
    0x8080,
    0xFFC0,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const uint16_t k_wht[16] = {
    0x0000, 0x0000,
    0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static constexpr uint32_t COL_BLACK = 0x00000000u;
static constexpr uint32_t COL_WHITE = 0x00FFFFFFu;
static constexpr int      CUR_W     = 16;
static constexpr int      CUR_H     = 16;

namespace cursor {

static int32_t  g_x = 0, g_y = 0;

static uint32_t g_save_buf[CUR_W * CUR_H];
static int32_t  g_save_x    = 0, g_save_y    = 0;
static int32_t  g_restore_x = 0, g_restore_y = 0;
static bool     g_has_save    = false;
static bool     g_has_restore = false;

void set_pos(int32_t x, int32_t y) { g_x = x; g_y = y; }
int32_t pos_x() { return g_x; }
int32_t pos_y() { return g_y; }

void save_bg() {
    uint32_t* fbo  = vgpu::framebuffer();
    uint32_t  scrw = vgpu::width();
    uint32_t  scrh = vgpu::height();
    g_save_x   = g_x;
    g_save_y   = g_y;
    g_has_save = true;
    for (int row = 0; row < CUR_H; ++row) {
        int32_t py = g_y + row;
        for (int col = 0; col < CUR_W; ++col) {
            int32_t px = g_x + col;
            if (py < 0 || (uint32_t)py >= scrh ||
                px < 0 || (uint32_t)px >= scrw) {
                g_save_buf[row * CUR_W + col] = 0u;
            } else {
                g_save_buf[row * CUR_W + col] =
                    fbo[(uint32_t)py * scrw + (uint32_t)px];
            }
        }
    }
}

void restore_bg() {
    if (!g_has_save) return;

    g_restore_x   = g_save_x;
    g_restore_y   = g_save_y;
    g_has_restore = true;
    uint32_t* fbo  = vgpu::framebuffer();
    uint32_t  scrw = vgpu::width();
    uint32_t  scrh = vgpu::height();
    for (int row = 0; row < CUR_H; ++row) {
        int32_t py = g_save_y + row;
        if (py < 0 || (uint32_t)py >= scrh) continue;
        for (int col = 0; col < CUR_W; ++col) {
            int32_t px = g_save_x + col;
            if (px < 0 || (uint32_t)px >= scrw) continue;
            fbo[(uint32_t)py * scrw + (uint32_t)px] =
                g_save_buf[row * CUR_W + col];
        }
    }
}

bool dirty_rect(int32_t& x, int32_t& y, uint32_t& w, uint32_t& h) {
    if (!g_has_save) { w = 0; h = 0; return false; }

    int32_t x1 = g_save_x;
    int32_t y1 = g_save_y;
    int32_t x2 = g_save_x + CUR_W;
    int32_t y2 = g_save_y + CUR_H;

    if (g_has_restore) {
        if (g_restore_x          < x1) x1 = g_restore_x;
        if (g_restore_y          < y1) y1 = g_restore_y;
        if (g_restore_x + CUR_W  > x2) x2 = g_restore_x + CUR_W;
        if (g_restore_y + CUR_H  > y2) y2 = g_restore_y + CUR_H;
        g_has_restore = false;
    }

    uint32_t scrw = vgpu::width(), scrh = vgpu::height();
    if (x1 < 0)               x1 = 0;
    if (y1 < 0)               y1 = 0;
    if ((uint32_t)x2 > scrw)  x2 = (int32_t)scrw;
    if ((uint32_t)y2 > scrh)  y2 = (int32_t)scrh;
    x = x1; y = y1;
    w = (uint32_t)(x2 - x1);
    h = (uint32_t)(y2 - y1);
    return w > 0 && h > 0;
}

void draw() {
    uint32_t* fbo  = vgpu::framebuffer();
    uint32_t  scrw = vgpu::width();
    uint32_t  scrh = vgpu::height();

    for (int row = 0; row < CUR_H; ++row) {
        int32_t py = g_y + row;
        if (py < 0 || (uint32_t)py >= scrh) continue;

        uint16_t bm_blk = k_blk[row];
        uint16_t bm_wht = k_wht[row];

        for (int col = 0; col < CUR_W; ++col) {
            int32_t px = g_x + col;
            if (px < 0 || (uint32_t)px >= scrw) continue;

            uint16_t bit = (uint16_t)(0x8000u >> col);
            if (bm_blk & bit)
                fbo[(uint32_t)py * scrw + (uint32_t)px] = COL_BLACK;
            else if (bm_wht & bit)
                fbo[(uint32_t)py * scrw + (uint32_t)px] = COL_WHITE;

        }
    }
}

}
