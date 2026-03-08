/*
  paint.cpp - simple pixel-art drawing app
  toolbar at the top has a 10-color palette and small/medium/large brush buttons
  left-click and drag in the canvas draws with the selected color
  clear button fills the canvas with white
*/
#include "kernel/apps/paint.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/draw.hpp"
#include <stdint.h>
#include <string.h>

static constexpr uint32_t PAINT_W        = 480u;
static constexpr uint32_t PAINT_CLIENT_H = 300u;
static constexpr uint32_t PAINT_H        = PAINT_CLIENT_H + wm::WIN_TITLEBAR_H;
static constexpr uint32_t FW             = PAINT_W;

static constexpr uint32_t TOOLBAR_H  = 40u;
static constexpr uint32_t CANVAS_Y0  = TOOLBAR_H;

static constexpr uint32_t PAL_X      =  4u;
static constexpr uint32_t PAL_Y      =  7u;
static constexpr uint32_t PAL_W      = 26u;
static constexpr uint32_t PAL_H      = 26u;
static constexpr uint32_t PAL_GAP    =  2u;
static constexpr uint32_t PAL_COUNT  = 10u;

static constexpr uint32_t k_palette[PAL_COUNT] = {
    0x00000000u,
    0x00FFFFFFu,
    gfx::RED,
    gfx::GREEN,
    gfx::BLUE,
    0x00FFFF00u,
    0x0000FFFFu,
    gfx::rgb(255, 0, 255),
    gfx::rgb(255, 128, 0),
    gfx::GRAY,
};

static constexpr uint32_t BRUSH_COUNT  = 3u;
static constexpr uint32_t k_brush_px[BRUSH_COUNT] = { 1u, 4u, 9u };

static constexpr uint32_t BRUSH_BTN_X   = PAL_X + PAL_COUNT * (PAL_W + PAL_GAP) + 10u;
static constexpr uint32_t BRUSH_BTN_W   = 28u;
static constexpr uint32_t BRUSH_BTN_H   = 26u;
static constexpr uint32_t BRUSH_BTN_Y   = 7u;
static constexpr uint32_t BRUSH_BTN_GAP = 3u;

static constexpr uint32_t CLEAR_BTN_W = 52u;
static constexpr uint32_t CLEAR_BTN_H = 26u;
static constexpr uint32_t CLEAR_BTN_Y = 7u;
static constexpr uint32_t CLEAR_BTN_X = PAINT_W - CLEAR_BTN_W - 4u;

namespace {

static wm::Window* g_win     = nullptr;
static bool        g_active  = false;
static uint32_t    g_sel_pal = 0u;
static uint32_t    g_brush   = 1u;
static bool        g_dirty     = true;
static bool        g_last_held = false;
static bool        g_in_stroke = false;
static int32_t     g_prev_x    = 0;
static int32_t     g_prev_y    = 0;

static void buf_text(uint32_t* fb, uint32_t x, uint32_t y,
                     const char* s, uint32_t fg, uint32_t bg) {
    for (const char* p = s; *p; ++p, x += gfx::FONT_W)
        gfx::draw_char_into(fb, FW, x, y, *p, fg, bg);
}

static void draw_toolbar(uint32_t* fb) {

    for (uint32_t y = 0; y < TOOLBAR_H; ++y)
        for (uint32_t x = 0; x < PAINT_W; ++x)
            fb[y * FW + x] = 0x00C0C0C0u;

    for (uint32_t x = 0; x < PAINT_W; ++x)
        fb[(TOOLBAR_H - 1u) * FW + x] = 0x00808080u;

    for (uint32_t i = 0; i < PAL_COUNT; ++i) {
        uint32_t sx  = PAL_X + i * (PAL_W + PAL_GAP);
        bool     sel = (i == g_sel_pal);

        for (uint32_t dy = 0; dy < PAL_H; ++dy)
            for (uint32_t dx = 0; dx < PAL_W; ++dx)
                fb[(PAL_Y + dy) * FW + sx + dx] = k_palette[i];

        uint32_t border_c = sel ? 0x00FFFFFFu : 0x00404040u;
        for (uint32_t dx = 0; dx < PAL_W; ++dx) {
            fb[(PAL_Y)          * FW + sx + dx] = border_c;
            fb[(PAL_Y + PAL_H - 1u) * FW + sx + dx] = border_c;
        }
        for (uint32_t dy = 1u; dy + 1u < PAL_H; ++dy) {
            fb[(PAL_Y + dy) * FW + sx]            = border_c;
            fb[(PAL_Y + dy) * FW + sx + PAL_W - 1u] = border_c;
        }

        if (sel) {
            uint32_t ring = 0x00000080u;
            for (uint32_t dx = 1u; dx + 1u < PAL_W; ++dx) {
                fb[(PAL_Y + 1u) * FW + sx + dx] = ring;
                fb[(PAL_Y + PAL_H - 2u) * FW + sx + dx] = ring;
            }
            for (uint32_t dy = 2u; dy + 2u < PAL_H; ++dy) {
                fb[(PAL_Y + dy) * FW + sx + 1u]          = ring;
                fb[(PAL_Y + dy) * FW + sx + PAL_W - 2u]  = ring;
            }
        }
    }

    static const char k_brush_label[BRUSH_COUNT] = { 'S', 'M', 'L' };
    for (uint32_t i = 0; i < BRUSH_COUNT; ++i) {
        uint32_t bx  = BRUSH_BTN_X + i * (BRUSH_BTN_W + BRUSH_BTN_GAP);
        bool     sel = (i == g_brush);
        uint32_t fill = sel ? 0x00000080u : 0x00C0C0C0u;
        uint32_t fg   = sel ? 0x00FFFFFFu : 0x00000000u;

        for (uint32_t dy = 0; dy < BRUSH_BTN_H; ++dy)
            for (uint32_t dx = 0; dx < BRUSH_BTN_W; ++dx)
                fb[(BRUSH_BTN_Y + dy) * FW + bx + dx] = fill;

        for (uint32_t dx = 0; dx < BRUSH_BTN_W; ++dx) {
            fb[(BRUSH_BTN_Y)                       * FW + bx + dx] = sel ? 0x00404040u : 0x00FFFFFFu;
            fb[(BRUSH_BTN_Y + BRUSH_BTN_H - 1u)   * FW + bx + dx] = sel ? 0x00FFFFFFu : 0x00404040u;
        }
        for (uint32_t dy = 1u; dy + 1u < BRUSH_BTN_H; ++dy) {
            fb[(BRUSH_BTN_Y + dy) * FW + bx]                     = sel ? 0x00404040u : 0x00FFFFFFu;
            fb[(BRUSH_BTN_Y + dy) * FW + bx + BRUSH_BTN_W - 1u]  = sel ? 0x00FFFFFFu : 0x00404040u;
        }

        uint32_t lx = bx + (BRUSH_BTN_W - gfx::FONT_W) / 2u;
        uint32_t ly = BRUSH_BTN_Y + (BRUSH_BTN_H - gfx::FONT_H) / 2u;
        gfx::draw_char_into(fb, FW, lx, ly, k_brush_label[i], fg, fill);
    }

    static constexpr uint32_t C_FILL = 0x00C0C0C0u;
    for (uint32_t dy = 0; dy < CLEAR_BTN_H; ++dy)
        for (uint32_t dx = 0; dx < CLEAR_BTN_W; ++dx)
            fb[(CLEAR_BTN_Y + dy) * FW + CLEAR_BTN_X + dx] = C_FILL;

    for (uint32_t dx = 0; dx < CLEAR_BTN_W; ++dx) {
        fb[(CLEAR_BTN_Y)                       * FW + CLEAR_BTN_X + dx] = 0x00FFFFFFu;
        fb[(CLEAR_BTN_Y + CLEAR_BTN_H - 1u)   * FW + CLEAR_BTN_X + dx] = 0x00404040u;
    }
    for (uint32_t dy = 1u; dy + 1u < CLEAR_BTN_H; ++dy) {
        fb[(CLEAR_BTN_Y + dy) * FW + CLEAR_BTN_X]                    = 0x00FFFFFFu;
        fb[(CLEAR_BTN_Y + dy) * FW + CLEAR_BTN_X + CLEAR_BTN_W - 1u] = 0x00404040u;
    }

    uint32_t clx = CLEAR_BTN_X + (CLEAR_BTN_W - 5u * gfx::FONT_W) / 2u;
    uint32_t cly = CLEAR_BTN_Y + (CLEAR_BTN_H - gfx::FONT_H) / 2u;
    buf_text(fb, clx, cly, "Clear", 0x00000000u, C_FILL);
}

static void clear_canvas(uint32_t* fb) {
    for (uint32_t y = CANVAS_Y0; y < PAINT_CLIENT_H; ++y)
        for (uint32_t x = 0; x < PAINT_W; ++x)
            fb[y * FW + x] = 0x00FFFFFFu;
}

static void paint_at(uint32_t* fb, int32_t cx, int32_t cy) {

    int32_t canvas_cy = cy - (int32_t)CANVAS_Y0;
    if (canvas_cy < 0) return;

    uint32_t radius = k_brush_px[g_brush];
    uint32_t color  = k_palette[g_sel_pal];

    int32_t half = (int32_t)(radius / 2u);
    int32_t x0 = cx - half;
    int32_t y0 = (int32_t)CANVAS_Y0 + canvas_cy - half;

    for (int32_t dy = 0; dy < (int32_t)radius; ++dy) {
        int32_t py = y0 + dy;
        if (py < (int32_t)CANVAS_Y0 || py >= (int32_t)PAINT_CLIENT_H) continue;
        for (int32_t dx = 0; dx < (int32_t)radius; ++dx) {
            int32_t px = x0 + dx;
            if (px < 0 || px >= (int32_t)PAINT_W) continue;
            fb[(uint32_t)py * FW + (uint32_t)px] = color;
        }
    }
}

static bool in_rect(int32_t cx, int32_t cy,
                    uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return cx >= (int32_t)x && cx < (int32_t)(x + w) &&
           cy >= (int32_t)y && cy < (int32_t)(y + h);
}

static void draw_line(uint32_t* fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int32_t dx  = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy  = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int32_t sx  = (x0 < x1) ? 1 : -1;
    int32_t sy  = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        paint_at(fb, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

}

namespace paint {

void open() {
    if (g_active) return;
    int32_t ox = (int32_t)((1280u - PAINT_W) / 2u);
    int32_t oy = (int32_t)((800u  - PAINT_H) / 2u);
    g_win = wm::win_create(ox, oy, PAINT_W, PAINT_H, "Paint");
    if (!g_win) return;
    g_active     = false;
    g_dirty      = true;
    g_sel_pal    = 0u;
    g_brush      = 1u;
    g_last_held  = false;
    g_in_stroke  = false;

    uint32_t* fb = g_win->client_fb;
    if (fb) {
        clear_canvas(fb);
        draw_toolbar(fb);
    }
    wm::win_mark_dirty(g_win);
    g_active = true;
}

void close() {
    if (!g_active) return;
    wm::win_destroy(g_win);
    g_win    = nullptr;
    g_active = false;
}

bool active() { return g_active; }

void tick(uint64_t ) {
    if (!g_active || !g_win) return;

    if (g_win->close_requested) { close(); return; }

    uint32_t* fb = g_win->client_fb;
    if (!fb) return;

    bool need_flush = false;

    if (g_win->client_held) {
        int32_t hx = g_win->held_cx;
        int32_t hy = g_win->held_cy;

        if (hy >= (int32_t)CANVAS_Y0) {
            if (g_in_stroke) {
                draw_line(fb, g_prev_x, g_prev_y, hx, hy);
            } else {
                paint_at(fb, hx, hy);
            }
            g_prev_x    = hx;
            g_prev_y    = hy;
            g_in_stroke = true;
            need_flush  = true;
        }
        g_last_held = true;
    } else {
        g_in_stroke = false;
        g_last_held = false;
    }

    if (g_win->client_clicked) {
        int32_t cx = g_win->click_cx;
        int32_t cy = g_win->click_cy;
        g_win->client_clicked = false;

        for (uint32_t i = 0; i < PAL_COUNT; ++i) {
            uint32_t sx = PAL_X + i * (PAL_W + PAL_GAP);
            if (in_rect(cx, cy, sx, PAL_Y, PAL_W, PAL_H)) {
                g_sel_pal = i;
                draw_toolbar(fb);
                need_flush = true;
                goto done_click;
            }
        }

        for (uint32_t i = 0; i < BRUSH_COUNT; ++i) {
            uint32_t bx = BRUSH_BTN_X + i * (BRUSH_BTN_W + BRUSH_BTN_GAP);
            if (in_rect(cx, cy, bx, BRUSH_BTN_Y, BRUSH_BTN_W, BRUSH_BTN_H)) {
                g_brush = i;
                draw_toolbar(fb);
                need_flush = true;
                goto done_click;
            }
        }

        if (in_rect(cx, cy, CLEAR_BTN_X, CLEAR_BTN_Y, CLEAR_BTN_W, CLEAR_BTN_H)) {
            clear_canvas(fb);
            need_flush = true;
            goto done_click;
        }

        if (cy >= (int32_t)CANVAS_Y0) {
            paint_at(fb, cx, cy);
            need_flush = true;
        }
        done_click:;
    }

    if (need_flush)
        wm::win_mark_dirty(g_win);
}

}
