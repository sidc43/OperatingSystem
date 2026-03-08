/*
  wm.cpp - window manager and compositor
  manages floating windows with drag, title bar, close and maximize buttons
  has a taskbar, start menu (with scrollable/searchable all-programs panel),
  and a desktop icon layer underneath everything
  rendering is double-dirty: full render when windows change, cursor-only fast
  path when only the mouse moved
*/
#include "kernel/wm/wm.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/cursor.hpp"
#include "kernel/drivers/virtio/gpu.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/print.hpp"
#include "kernel/gfx/assets/icon_shell.hpp"
#include "kernel/gfx/assets/icon_calc.hpp"
#include "kernel/gfx/assets/icon_files.hpp"
#include "kernel/gfx/assets/icon_editor.hpp"
#include "kernel/gfx/assets/icon_controlpanel.hpp"
#include "kernel/gfx/assets/icon_sysmon.hpp"
#include "kernel/gfx/assets/icon_paint.hpp"
#include "kernel/gfx/assets/icon_startmenu.hpp"
#include <string.h>
#include <stdint.h>

namespace desktop { void render(); }

namespace {

static constexpr uint32_t MAX_COLS    = 256;
static constexpr uint32_t MAX_ROWS    = 128;

static constexpr uint32_t COL_TITLEBAR_BG = 0x002e1a1au;
static constexpr uint32_t COL_TITLEBAR_FG = 0x00ffe0e0u;
static constexpr uint32_t COL_ACCENT      = 0x00b6599bu;

static constexpr uint32_t COL_TERM_FG     = 0x00FFFFFFu;
static constexpr uint32_t COL_CURSOR_BG   = 0x00b0b0b0u;
static constexpr uint32_t COL_CURSOR_FG   = 0x00170d0du;

static constexpr uint32_t COL_WIN_TITLE_BG  = 0x00253535u;
static constexpr uint32_t COL_WIN_TITLE_FG  = 0x00e0e0e0u;
static constexpr uint32_t COL_WIN_BORDER    = 0x00607070u;
static constexpr uint32_t COL_WIN_CLIENT_BG = 0x00111111u;

static uint32_t g_sw = 0, g_sh = 0;
static uint32_t g_term_x = 0;
static uint32_t g_term_y = wm::WM_TITLEBAR_H;
static uint32_t g_cols = 0, g_rows = 0;

static char     g_cell[MAX_ROWS][MAX_COLS];
static bool     g_dirty[MAX_ROWS][MAX_COLS];
static bool     g_all_dirty = true;

static uint32_t g_cur_col = 0;
static uint32_t g_cur_row = 0;
static uint32_t g_prev_cur_col = 0;
static uint32_t g_prev_cur_row = 0;

static char g_status[64] = "";
static bool g_title_dirty = true;

static wm::Window g_windows[wm::MAX_WINDOWS];
static uint8_t    g_zorder[wm::MAX_WINDOWS];
static int        g_nwindows = 0;

static bool g_desktop_dirty = true;

static bool g_cursor_dirty  = false;

static uint32_t g_wallpaper_color = 0x00008080u;

static bool g_term_visible = false;

static bool    g_dbclick        = false;
static int32_t g_dbclick_x      = 0;
static int32_t g_dbclick_y      = 0;

static bool    g_prev_btn       = false;
static bool    g_prev_btn_right = false;
static bool    g_dragging    = false;
static int     g_drag_win    = -1;
static int32_t g_drag_off_x  = 0;
static int32_t g_drag_off_y  = 0;

static constexpr uint32_t SM_MENU_W   = 300u;
static constexpr uint32_t SM_ITEM_H   =  48u;
static constexpr uint32_t SM_AP_ITEM_H=  32u;
static constexpr uint32_t SM_HEADER_H =  34u;
static constexpr uint32_t SM_DIVIDER_H=   8u;
static constexpr uint32_t SM_ICON_SZ  =  32u;
static constexpr uint32_t SM_ICON_X   =   8u;
static constexpr uint32_t SM_TEXT_X   =  48u;

struct SmItem { const char* label; int dispatch; int icon; };

static constexpr int SM_DEFAULT_COUNT = 4;
static constexpr int SM_DEFAULT_DIV   = 3;
static const SmItem  k_sm_default[4]  = {
    { "Shell",           0,  0 },
    { "Text Editor",     1,  1 },
    { "All Programs",    -2, -1 },
    { "Shut Down",       7,  7 },
};

static constexpr uint32_t SM_ALL_W    = 340u;
static constexpr int      SM_AP_COUNT = 7;
static const SmItem k_sm_allprog[7] = {
    { "Calculator",     3, 3 },
    { "Control Panel",  2, 2 },
    { "Files",          4, 4 },
    { "Paint",          6, 6 },
    { "Shell",          0, 0 },
    { "System Monitor", 5, 5 },
    { "Text Editor",    1, 1 },
};

static bool g_sm_all_progs = false;
static bool g_start_open   = false;
static int  g_start_sel    = -1;

static constexpr int      SM_AP_PAGE  = 7;
static constexpr uint32_t SM_ARROW_H  = 20u;
static constexpr uint32_t SM_SEARCH_H = 30u;
static int  g_sm_ap_scroll   = 0;
static char g_sm_search[32]  = {};
static int  g_sm_search_len  = 0;

static int g_sm_filtered[SM_AP_COUNT];
static int g_sm_filtered_count = 0;

static inline bool sm_icase_eq(char a, char b) {
    auto lo = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    };
    return lo(a) == lo(b);
}

static void sm_rebuild_filter() {
    g_sm_filtered_count = 0;
    for (int i = 0; i < SM_AP_COUNT; ++i) {
        if (g_sm_search_len == 0) {
            g_sm_filtered[g_sm_filtered_count++] = i;
            continue;
        }
        const char* lbl = k_sm_allprog[i].label;
        int llen = 0;
        while (lbl[llen]) ++llen;
        bool found = false;
        for (int s = 0; s <= llen - g_sm_search_len && !found; ++s) {
            bool ok = true;
            for (int k = 0; k < g_sm_search_len && ok; ++k)
                ok = sm_icase_eq(lbl[s + k], g_sm_search[k]);
            if (ok) found = true;
        }
        if (found) g_sm_filtered[g_sm_filtered_count++] = i;
    }
    g_sm_ap_scroll = 0;
}

static void scroll_up() {
    for (uint32_t r = 1; r < g_rows; ++r) {
        for (uint32_t c = 0; c < g_cols; ++c) {
            g_cell[r - 1][c] = g_cell[r][c];
            g_dirty[r - 1][c] = true;
        }
    }
    for (uint32_t c = 0; c < g_cols; ++c) {
        g_cell[g_rows - 1][c] = ' ';
        g_dirty[g_rows - 1][c] = true;
    }
    g_cur_row = g_rows - 1;
}

static void draw_title() {
    if (!g_all_dirty && !g_title_dirty) return;

    gfx::fill_rect(0, 0, g_sw, wm::WM_TITLEBAR_H, COL_TITLEBAR_BG);
    gfx::draw_hline(0, wm::WM_TITLEBAR_H - 2, g_sw, COL_ACCENT);
    gfx::draw_hline(0, wm::WM_TITLEBAR_H - 1, g_sw, COL_ACCENT);

    static const char* TITLE = "  AArch64 OS  ";
    uint32_t tx = 8;
    uint32_t ty = (wm::WM_TITLEBAR_H - gfx::FONT_H) / 2;
    gfx::draw_text(tx, ty, TITLE, COL_TITLEBAR_FG, COL_TITLEBAR_BG);

    if (g_status[0]) {
        uint32_t slen = 0;
        while (g_status[slen]) ++slen;
        uint32_t sx = g_sw - slen * gfx::FONT_W - 8;
        gfx::draw_text(sx, ty, g_status, COL_ACCENT, COL_TITLEBAR_BG);
    }
    g_title_dirty = false;
}

static void draw_cell(uint32_t row, uint32_t col, bool cursor_here) {
    char c = g_cell[row][col];

    if (!cursor_here && (!c || c == ' ')) return;
    uint32_t px = g_term_x + col * gfx::FONT_W;
    uint32_t py = g_term_y + row * gfx::FONT_H;
    if (!c) c = ' ';
    uint32_t fg = cursor_here ? COL_CURSOR_FG : COL_TERM_FG;
    uint32_t bg = cursor_here ? COL_CURSOR_BG : g_wallpaper_color;
    gfx::draw_char(px, py, c, fg, bg);
}

static void composite_window(const wm::Window& w) {
    if (!w.visible) return;

    gfx::fill_rect((uint32_t)w.x, (uint32_t)w.y,
                   w.w, wm::WIN_TITLEBAR_H, COL_WIN_TITLE_BG);

    gfx::draw_hline((uint32_t)w.x,
                    (uint32_t)w.y + wm::WIN_TITLEBAR_H - 1,
                    w.w, COL_ACCENT);

    uint32_t ty = (uint32_t)w.y + (wm::WIN_TITLEBAR_H - gfx::FONT_H) / 2;
    gfx::draw_text((uint32_t)w.x + 4, ty, w.title,
                   COL_WIN_TITLE_FG, COL_WIN_TITLE_BG);

    {
        uint32_t cbx = (uint32_t)(w.x + (int32_t)w.w) - wm::WIN_CLOSE_BTN_W - 2u;
        uint32_t cby = (uint32_t)w.y + 2u;
        uint32_t cbh = wm::WIN_TITLEBAR_H - 4u;
        uint32_t cbc = 0x00C0C0C0u;
        gfx::fill_rect(cbx, cby, wm::WIN_CLOSE_BTN_W, cbh, cbc);

        gfx::draw_hline(cbx,                      cby,           wm::WIN_CLOSE_BTN_W, 0x00FFFFFFu);
        gfx::draw_hline(cbx,                      cby + cbh - 1, wm::WIN_CLOSE_BTN_W, 0x00404040u);
        gfx::draw_vline(cbx,                      cby,           cbh,                 0x00FFFFFFu);
        gfx::draw_vline(cbx + wm::WIN_CLOSE_BTN_W - 1u, cby,    cbh,                 0x00404040u);

        uint32_t cx = cbx + (wm::WIN_CLOSE_BTN_W - gfx::FONT_W) / 2u;
        uint32_t cy = cby + (cbh > gfx::FONT_H ? (cbh - gfx::FONT_H) / 2u : 0u);
        gfx::draw_char(cx, cy, 'X', 0x00000000u, cbc);
    }

    if (w.client_fb && w.client_h > 0) {
        gfx::blit(w.client_fb,
                  (uint32_t)w.x,
                  (uint32_t)w.y + wm::WIN_TITLEBAR_H,
                  w.w, w.client_h);
    }

    gfx::draw_rect((uint32_t)w.x, (uint32_t)w.y, w.w, w.h, COL_WIN_BORDER);
}

static void composite_all_windows() {
    for (int i = 0; i < g_nwindows; ++i) {
        int wi = g_zorder[i];
        composite_window(g_windows[wi]);
    }
}

static void zorder_bring_front(int wi) {
    int pos = -1;
    for (int i = 0; i < g_nwindows; ++i) {
        if (g_zorder[i] == (uint8_t)wi) { pos = i; break; }
    }
    if (pos < 0 || pos == g_nwindows - 1) return;

    for (int i = pos; i < g_nwindows - 1; ++i)
        g_zorder[i] = g_zorder[i + 1];
    g_zorder[g_nwindows - 1] = (uint8_t)wi;
}

static int hit_test_titlebar(int32_t px, int32_t py) {
    for (int i = g_nwindows - 1; i >= 0; --i) {
        int wi = g_zorder[i];
        const wm::Window& w = g_windows[wi];
        if (!w.visible) continue;
        if (px >= w.x && px < w.x + (int32_t)w.w &&
            py >= w.y && py < w.y + (int32_t)wm::WIN_TITLEBAR_H) {
            return wi;
        }
    }
    return -1;
}

static int hit_test_close_btn(int32_t px, int32_t py) {
    for (int i = g_nwindows - 1; i >= 0; --i) {
        int wi = g_zorder[i];
        const wm::Window& w = g_windows[wi];
        if (!w.visible) continue;
        int32_t cbx = w.x + (int32_t)w.w - (int32_t)wm::WIN_CLOSE_BTN_W - 2;
        int32_t cby = w.y + 2;
        int32_t cbh = (int32_t)wm::WIN_TITLEBAR_H - 4;
        if (px >= cbx && px < cbx + (int32_t)wm::WIN_CLOSE_BTN_W &&
            py >= cby && py < cby + cbh)
            return wi;
    }
    return -1;
}

static int hit_test_max_btn(int32_t px, int32_t py) {
    for (int i = g_nwindows - 1; i >= 0; --i) {
        int wi = g_zorder[i];
        const wm::Window& w = g_windows[wi];
        if (!w.visible) continue;
        int32_t cbx = w.x + (int32_t)w.w - (int32_t)wm::WIN_CLOSE_BTN_W - 2;
        int32_t mbx = cbx - (int32_t)wm::WIN_MAX_BTN_W - 2;
        int32_t mby = w.y + 2;
        int32_t mbh = (int32_t)wm::WIN_TITLEBAR_H - 4;
        if (px >= mbx && px < mbx + (int32_t)wm::WIN_MAX_BTN_W &&
            py >= mby && py < mby + mbh)
            return wi;
    }
    return -1;
}

static void win_toggle_maximize(int wi) {
    wm::Window& w = g_windows[wi];
    if (!w.maximized) {
        w.restore_x = w.x;
        w.restore_y = w.y;
        w.restore_w = w.w;
        w.restore_h = w.h;
        w.x         = 0;
        w.y         = (int32_t)wm::WM_TITLEBAR_H;
        w.w         = g_sw;
        w.h         = g_sh - wm::WM_TITLEBAR_H - wm::TASKBAR_H;
        w.client_h  = (w.h > wm::WIN_TITLEBAR_H) ? w.h - wm::WIN_TITLEBAR_H : 0u;
        w.maximized = true;
    } else {
        w.x         = w.restore_x;
        w.y         = w.restore_y;
        w.w         = w.restore_w;
        w.h         = w.restore_h;
        w.client_h  = (w.restore_h > wm::WIN_TITLEBAR_H)
                      ? w.restore_h - wm::WIN_TITLEBAR_H : 0u;
        w.maximized = false;
    }
    w.dirty         = true;
    g_desktop_dirty = true;
}

static int hit_test_client(int32_t px, int32_t py) {
    for (int i = g_nwindows - 1; i >= 0; --i) {
        int wi = g_zorder[i];
        const wm::Window& w = g_windows[wi];
        if (!w.visible) continue;
        int32_t cy_top = w.y + (int32_t)wm::WIN_TITLEBAR_H;
        if (px >= w.x && px < w.x + (int32_t)w.w &&
            py >= cy_top && py < w.y + (int32_t)w.h)
            return wi;
    }
    return -1;
}

static bool hit_test_any_window(int32_t px, int32_t py) {
    for (int i = 0; i < g_nwindows; ++i) {
        const wm::Window& w = g_windows[g_zorder[i]];
        if (!w.visible) continue;
        if (px >= w.x && px < w.x + (int32_t)w.w &&
            py >= w.y && py < w.y + (int32_t)w.h)
            return true;
    }
    return false;
}

static void handle_taskbar_click(int32_t px) {
    int32_t bx = (int32_t)(4u + wm::START_BTN_W + 8u);
    static constexpr int32_t BTN_W = 120;
    static constexpr int32_t GAP   =  4;
    for (int i = 0; i < g_nwindows; ++i) {
        int wi = g_zorder[i];
        if (!g_windows[wi].visible) continue;
        if (px >= bx && px < bx + BTN_W) {
            zorder_bring_front(wi);
            g_desktop_dirty = true;
            return;
        }
        bx += BTN_W + GAP;
        if (bx + BTN_W > (int32_t)g_sw - 4) break;
    }
}

static void draw_sm_icon(int i, uint32_t ix, uint32_t iy, uint32_t bg) {
    if (i < 0) return;
    static const uint32_t* const k_sprites[7] = {
        gfx::assets::icon_shell,
        gfx::assets::icon_editor,
        gfx::assets::icon_controlpanel,
        gfx::assets::icon_calc,
        gfx::assets::icon_files,
        gfx::assets::icon_sysmon,
        gfx::assets::icon_paint,
    };
    if (i >= 0 && i < 7) {
        const uint32_t* src = k_sprites[i];
        static constexpr uint32_t SRC_W = 48u;
        static constexpr uint32_t DST_W = 32u;
        static constexpr uint32_t DST_H = 32u;
        for (uint32_t dy = 0; dy < DST_H; ++dy) {
            uint32_t sy = dy * SRC_W / DST_W;
            for (uint32_t dx = 0; dx < DST_W; ++dx) {
                uint32_t sx  = dx * SRC_W / DST_W;
                uint32_t pix = src[sy * SRC_W + sx];
                uint8_t a = (uint8_t)(pix >> 24);
                if (a == 0) {
                    gfx::draw_pixel(ix + dx, iy + dy, bg);
                } else if (a == 255) {
                    gfx::draw_pixel(ix + dx, iy + dy, pix & 0x00FFFFFFu);
                } else {
                    uint32_t rb_fg = pix & 0x00FF00FFu;
                    uint32_t g_fg  = pix & 0x0000FF00u;
                    uint32_t rb_bg = bg  & 0x00FF00FFu;
                    uint32_t g_bg  = bg  & 0x0000FF00u;
                    uint32_t rb = (rb_fg * a + rb_bg * (255u - a)) >> 8u;
                    uint32_t gn = (g_fg  * a + g_bg  * (255u - a)) >> 8u;
                    gfx::draw_pixel(ix + dx, iy + dy, (rb & 0x00FF00FFu) | (gn & 0x0000FF00u));
                }
            }
        }
    } else if (i == 7) {

        static constexpr uint32_t S = 32u;
        gfx::fill_rect(ix, iy, S, S, bg);
        gfx::fill_rect(ix+11u, iy+ 5u,  10u, 20u, 0x00AA0000u);
        gfx::fill_rect(ix+ 5u, iy+11u,  22u,  9u, 0x00AA0000u);
        gfx::fill_rect(ix+11u, iy+11u,  10u,  9u, bg);
        gfx::fill_rect(ix+10u, iy+10u,  12u, 11u, bg);
        gfx::fill_rect(ix+14u, iy+ 2u,   4u, 13u, 0x00AA0000u);
        gfx::fill_rect(ix+14u, iy+11u,   4u,  4u, bg);
    } else if (i == 8) {

        gfx::fill_rect(ix, iy, 32u, 32u, bg);

        gfx::fill_rect(ix, iy, 32u, 32u, bg);
        for (int32_t r = -8; r <= 8; ++r) {
            int32_t len = 8 - (r < 0 ? -r : r) + 1;
            for (int32_t dx = 0; dx < len; ++dx)
                gfx::draw_pixel(ix + 8u + (uint32_t)dx,
                                iy + 16u + (uint32_t)r, 0x00505050u);
        }
    } else if (i == 9) {
        gfx::fill_rect(ix, iy, 32u, 32u, bg);
    }
}

static void draw_taskbar() {
    uint32_t ty = g_sh - wm::TASKBAR_H;
    gfx::fill_rect(0, ty, g_sw, wm::TASKBAR_H, 0x00C0C0C0u);
    gfx::draw_hline(0, ty,     g_sw, 0x00FFFFFFu);
    gfx::draw_hline(0, ty + 1, g_sw, 0x00808080u);

    uint32_t sb_y  = ty + 3u;
    uint32_t sb_h  = wm::TASKBAR_H - 6u;
    uint32_t sb_bg = g_start_open ? 0x00808080u : 0x00C0C0C0u;
    gfx::fill_rect(4u, sb_y, wm::START_BTN_W, sb_h, sb_bg);
    gfx::draw_hline(4u,                        sb_y,             wm::START_BTN_W, g_start_open ? 0x00404040u : 0x00FFFFFFu);
    gfx::draw_hline(4u,                        sb_y + sb_h - 1u, wm::START_BTN_W, g_start_open ? 0x00FFFFFFu : 0x00404040u);
    gfx::draw_vline(4u,                        sb_y,             sb_h,            g_start_open ? 0x00404040u : 0x00FFFFFFu);
    gfx::draw_vline(4u + wm::START_BTN_W - 1u, sb_y,            sb_h,            g_start_open ? 0x00FFFFFFu : 0x00404040u);
    {

        static constexpr uint32_t SRC_W = 48u;
        uint32_t dsz    = sb_h - 2u;
        uint32_t icon_lx = 6u;
        uint32_t icon_ly = sb_y + 1u;
        const uint32_t* src = gfx::assets::icon_startmenu;
        for (uint32_t dy = 0; dy < dsz; ++dy) {
            uint32_t sy = dy * SRC_W / dsz;
            for (uint32_t dx = 0; dx < dsz; ++dx) {
                uint32_t sx  = dx * SRC_W / dsz;
                uint32_t pix = src[sy * SRC_W + sx];
                uint8_t  a   = (uint8_t)(pix >> 24);
                uint32_t out;
                if (a == 0) {
                    out = sb_bg;
                } else if (a == 255) {
                    out = pix & 0x00FFFFFFu;
                } else {
                    uint32_t rb_fg = pix & 0x00FF00FFu, g_fg = pix & 0x0000FF00u;
                    uint32_t rb_bg = sb_bg & 0x00FF00FFu, g_bk = sb_bg & 0x0000FF00u;
                    uint32_t rb = (rb_fg * a + rb_bg * (255u - a)) >> 8u;
                    uint32_t gn = (g_fg  * a + g_bk  * (255u - a)) >> 8u;
                    out = (rb & 0x00FF00FFu) | (gn & 0x0000FF00u);
                }
                gfx::draw_pixel(icon_lx + dx, icon_ly + dy, out);
            }
        }

        static const char* SL = "Start";
        uint32_t slx = icon_lx + dsz + 3u;
        uint32_t sly = sb_y + (sb_h > gfx::FONT_H ? (sb_h - gfx::FONT_H) / 2u : 0u);
        gfx::draw_text(slx, sly, SL, 0x00000000u, sb_bg);
    }

    if (g_start_open) {

        uint32_t mh = SM_HEADER_H + (uint32_t)SM_DEFAULT_COUNT * SM_ITEM_H + SM_DIVIDER_H;
        uint32_t my = ty - mh;
        gfx::fill_rect(0u, my, SM_MENU_W, mh, 0x00C0C0C0u);
        gfx::draw_rect(0u, my, SM_MENU_W, mh, 0x00000000u);

        gfx::fill_rect(1u, my + 1u, SM_MENU_W - 2u, SM_HEADER_H - 1u, 0x00000080u);
        {
            static const char* HDR = "Applications";
            uint32_t hx = (SM_MENU_W - 12u * gfx::FONT_W) / 2u;
            uint32_t hy = my + (SM_HEADER_H - gfx::FONT_H) / 2u;
            gfx::draw_text(hx, hy, HDR, 0x00FFFFFFu, 0x00000080u);
        }
        uint32_t iy = my + SM_HEADER_H;
        for (int i = 0; i < SM_DEFAULT_COUNT; ++i) {
            if (i == SM_DEFAULT_DIV) {
                gfx::draw_hline(4u, iy + 2u, SM_MENU_W - 8u, 0x00808080u);
                gfx::draw_hline(4u, iy + 3u, SM_MENU_W - 8u, 0x00FFFFFFu);
                iy += SM_DIVIDER_H;
            }

            bool hi = (g_sm_all_progs && k_sm_default[i].dispatch == -2);
            uint32_t row_bg = hi ? 0x00000080u : 0x00C0C0C0u;
            uint32_t row_fg = hi ? 0x00FFFFFFu : 0x00000000u;
            gfx::fill_rect(1u, iy, SM_MENU_W - 2u, SM_ITEM_H, row_bg);
            uint32_t icon_y = iy + (SM_ITEM_H - SM_ICON_SZ) / 2u;
            draw_sm_icon(k_sm_default[i].icon, SM_ICON_X, icon_y, row_bg);
            uint32_t text_y = iy + (SM_ITEM_H - gfx::FONT_H) / 2u;
            uint32_t tx = (k_sm_default[i].icon < 0) ? SM_ICON_X : SM_TEXT_X;
            gfx::draw_text(tx, text_y, k_sm_default[i].label, row_fg, row_bg);

            if (k_sm_default[i].dispatch == -2) {
                uint32_t ax = SM_MENU_W - gfx::FONT_W - 8u;
                gfx::draw_text(ax, text_y, ">", row_fg, row_bg);
            }
            iy += SM_ITEM_H;
        }

        if (g_sm_all_progs) {
            int      total        = g_sm_filtered_count;
            bool     needs_scroll = total > SM_AP_PAGE;
            int      visible      = (total > 0) ? (needs_scroll ? SM_AP_PAGE : total) : 1;
            uint32_t list_h       = (uint32_t)visible * SM_AP_ITEM_H;
            uint32_t aph          = SM_HEADER_H + SM_SEARCH_H + list_h
                                    + (needs_scroll ? 2u * SM_ARROW_H : 0u);
            uint32_t apy = ty - aph;
            uint32_t apx = SM_MENU_W;
            gfx::fill_rect(apx, apy, SM_ALL_W, aph, 0x00C0C0C0u);
            gfx::draw_rect(apx, apy, SM_ALL_W, aph, 0x00000000u);

            gfx::fill_rect(apx + 1u, apy + 1u, SM_ALL_W - 2u, SM_HEADER_H - 1u, 0x00000080u);
            {
                static const char* HDR2 = "All Programs";
                uint32_t hx = apx + (SM_ALL_W - 12u * gfx::FONT_W) / 2u;
                uint32_t hy = apy + (SM_HEADER_H - gfx::FONT_H) / 2u;
                gfx::draw_text(hx, hy, HDR2, 0x00FFFFFFu, 0x00000080u);
            }
            uint32_t aiy = apy + SM_HEADER_H;

            {
                gfx::fill_rect(apx + 1u, aiy, SM_ALL_W - 2u, SM_SEARCH_H, 0x00C0C0C0u);
                uint32_t sbx = apx + 6u;
                uint32_t sby = aiy + 5u;
                uint32_t sbw = SM_ALL_W - 12u;
                uint32_t sbh = SM_SEARCH_H - 10u;
                gfx::fill_rect(sbx, sby, sbw, sbh, 0x00FFFFFFu);
                gfx::draw_rect(sbx, sby, sbw, sbh, 0x00606060u);
                uint32_t sty = sby + (sbh > gfx::FONT_H ? (sbh - gfx::FONT_H) / 2u : 0u);
                if (g_sm_search_len == 0) {
                    gfx::draw_text(sbx + 3u, sty, "Search...", 0x00888888u, 0x00FFFFFFu);
                } else {
                    static char disp[34];
                    int di = 0;
                    for (int k = 0; k < g_sm_search_len; ++k) disp[di++] = g_sm_search[k];
                    disp[di++] = '_';
                    disp[di]   = '\0';
                    gfx::draw_text(sbx + 3u, sty, disp, 0x00000000u, 0x00FFFFFFu);
                }
                aiy += SM_SEARCH_H;
            }

            if (needs_scroll) {
                bool     can_up = g_sm_ap_scroll > 0;
                uint32_t abg    = can_up ? 0x00A0A0A0u : 0x00C0C0C0u;
                uint32_t afg    = can_up ? 0x00000000u : 0x00888888u;
                gfx::fill_rect(apx + 1u, aiy, SM_ALL_W - 2u, SM_ARROW_H, abg);
                uint32_t ax = apx + (SM_ALL_W - gfx::FONT_W) / 2u;
                uint32_t ay = aiy + (SM_ARROW_H - gfx::FONT_H) / 2u;
                gfx::draw_text(ax, ay, "^", afg, abg);
                aiy += SM_ARROW_H;
            }

            int end = g_sm_ap_scroll + visible;
            if (end > total) end = total;
            for (int i = g_sm_ap_scroll; i < end; ++i) {
                int src = g_sm_filtered[i];
                gfx::fill_rect(apx + 1u, aiy, SM_ALL_W - 2u, SM_AP_ITEM_H, 0x00C0C0C0u);
                uint32_t icon_y = aiy + (SM_AP_ITEM_H > SM_ICON_SZ
                                         ? (SM_AP_ITEM_H - SM_ICON_SZ) / 2u : 0u);
                draw_sm_icon(k_sm_allprog[src].icon, apx + SM_ICON_X, icon_y, 0x00C0C0C0u);
                uint32_t text_y = aiy + (SM_AP_ITEM_H > gfx::FONT_H
                                         ? (SM_AP_ITEM_H - gfx::FONT_H) / 2u : 0u);
                gfx::draw_text(apx + SM_TEXT_X, text_y,
                               k_sm_allprog[src].label, 0x00000000u, 0x00C0C0C0u);
                aiy += SM_AP_ITEM_H;
            }

            if (total == 0) {
                gfx::fill_rect(apx + 1u, aiy, SM_ALL_W - 2u, SM_AP_ITEM_H, 0x00C0C0C0u);
                uint32_t text_y = aiy + (SM_AP_ITEM_H > gfx::FONT_H
                                         ? (SM_AP_ITEM_H - gfx::FONT_H) / 2u : 0u);
                gfx::draw_text(apx + SM_ICON_X, text_y, "No results.", 0x00888888u, 0x00C0C0C0u);
            }

            if (needs_scroll) {
                bool     can_down = (g_sm_ap_scroll + visible) < total;
                uint32_t abg      = can_down ? 0x00A0A0A0u : 0x00C0C0C0u;
                uint32_t afg      = can_down ? 0x00000000u : 0x00888888u;
                gfx::fill_rect(apx + 1u, aiy, SM_ALL_W - 2u, SM_ARROW_H, abg);
                uint32_t ax = apx + (SM_ALL_W - gfx::FONT_W) / 2u;
                uint32_t ay = aiy + (SM_ARROW_H - gfx::FONT_H) / 2u;
                gfx::draw_text(ax, ay, "v", afg, abg);
            }
        }
    }

    uint32_t bx = 4u + wm::START_BTN_W + 8u;
    static constexpr uint32_t BTN_W    = 120u;
    static constexpr uint32_t BTN_H    = wm::TASKBAR_H - 6u;
    static constexpr uint32_t BTN_Y_OFF = 3u;
    for (int i = 0; i < g_nwindows; ++i) {
        int wi = g_zorder[i];
        const wm::Window& w = g_windows[wi];
        if (!w.visible) continue;
        if (bx + BTN_W > g_sw - 4u) break;
        uint32_t by = ty + BTN_Y_OFF;
        gfx::fill_rect(bx, by, BTN_W, BTN_H, 0x00C0C0C0u);
        gfx::draw_hline(bx,           by,           BTN_W, 0x00FFFFFFu);
        gfx::draw_hline(bx,           by + BTN_H-1, BTN_W, 0x00404040u);
        gfx::draw_vline(bx,           by,           BTN_H, 0x00FFFFFFu);
        gfx::draw_vline(bx + BTN_W-1, by,           BTN_H, 0x00404040u);
        uint32_t tx    = bx + 4u;
        uint32_t texty = by + (BTN_H > gfx::FONT_H ? (BTN_H - gfx::FONT_H) / 2u : 0u);
        gfx::draw_text(tx, texty, w.title, 0x00000000u, 0x00C0C0C0u);
        bx += BTN_W + 4u;
    }
}

}

namespace wm {

void init(uint32_t screen_w, uint32_t screen_h) {
    g_sw   = screen_w;
    g_sh   = screen_h;
    g_cols = screen_w / gfx::FONT_W;
    g_rows = (screen_h - wm::WM_TITLEBAR_H - wm::TASKBAR_H) / gfx::FONT_H;
    if (g_cols > MAX_COLS) g_cols = MAX_COLS;
    if (g_rows > MAX_ROWS) g_rows = MAX_ROWS;

    memset(g_cell,  ' ', sizeof(g_cell));
    memset(g_dirty, 1,   sizeof(g_dirty));

    g_cur_col = g_cur_row = 0;
    g_all_dirty = true;
}

void set_status(const char* s) {
    uint32_t i = 0;
    while (s[i] && i < 63) { g_status[i] = s[i]; ++i; }
    g_status[i]   = '\0';
    g_title_dirty = true;
}

void term_putc(char c) {

    g_dirty[g_cur_row][g_cur_col] = true;

    if (c == '\n') {
        g_cur_col = 0;
        if (g_cur_row + 1 < g_rows) {
            g_cur_row++;
        } else {
            scroll_up();
        }
    } else if (c == '\r') {
        g_cur_col = 0;
    } else if (c == '\b') {
        if (g_cur_col > 0) {
            --g_cur_col;
            g_cell[g_cur_row][g_cur_col] = ' ';
            g_dirty[g_cur_row][g_cur_col] = true;
        }
    } else {
        if (g_cur_col >= g_cols) {

            g_cur_col = 0;
            if (g_cur_row + 1 < g_rows) {
                g_cur_row++;
            } else {
                scroll_up();
            }
        }
        g_cell[g_cur_row][g_cur_col]  = c;
        g_dirty[g_cur_row][g_cur_col] = true;
        g_cur_col++;
    }
}

void term_puts(const char* s) {
    while (*s) term_putc(*s++);
}

void term_clear() {
    memset(g_cell,  ' ', sizeof(g_cell));
    memset(g_dirty, 1,   sizeof(g_dirty));
    g_cur_col = g_cur_row = 0;
    g_all_dirty = true;
}

void render() {
    if (!vgpu::ready()) return;

    gfx::fill_rect(g_term_x, g_term_y, g_sw, g_sh - wm::WM_TITLEBAR_H - wm::TASKBAR_H, g_wallpaper_color);
    draw_title();

    desktop::render();

    if (g_term_visible) {
        for (uint32_t r = 0; r < g_rows; ++r) {
            for (uint32_t c = 0; c < g_cols; ++c) {
                bool cur = (r == g_cur_row && c == g_cur_col);
                draw_cell(r, c, cur);
                g_dirty[r][c] = false;
            }
        }
    }
    g_all_dirty    = false;
    g_desktop_dirty = false;

    composite_all_windows();
    for (int i = 0; i < g_nwindows; ++i)
        g_windows[i].dirty = false;

    draw_taskbar();

    cursor::save_bg();
    cursor::draw();

    vgpu::flush_full();
}

void render_dirty() {
    if (!vgpu::ready()) return;

    if (g_cursor_dirty && !g_desktop_dirty && g_nwindows == 0) {
        g_cursor_dirty = false;
        cursor::restore_bg();
        cursor::save_bg();
        cursor::draw();
        int32_t  rx; int32_t  ry; uint32_t rw; uint32_t rh;
        if (cursor::dirty_rect(rx, ry, rw, rh))
            vgpu::flush_rect((uint32_t)rx, (uint32_t)ry, rw, rh);
        return;
    }
    g_cursor_dirty = false;

    if (g_desktop_dirty || g_nwindows > 0) {
        render();
        return;
    }

    if (g_title_dirty || g_all_dirty) {
        draw_title();
    }
    if (g_term_visible) {
        for (uint32_t r = 0; r < g_rows; ++r) {
            for (uint32_t c = 0; c < g_cols; ++c) {
                bool was_cur = (r == g_prev_cur_row && c == g_prev_cur_col);
                bool is_cur  = (r == g_cur_row      && c == g_cur_col);
                if (g_dirty[r][c] || was_cur || is_cur || g_all_dirty) {
                    draw_cell(r, c, is_cur);
                    g_dirty[r][c] = false;
                }
            }
        }
        g_prev_cur_col = g_cur_col;
        g_prev_cur_row = g_cur_row;
    }
    g_all_dirty    = false;

    draw_taskbar();

    cursor::restore_bg();
    cursor::save_bg();
    cursor::draw();

    vgpu::flush_full();
}

uint32_t term_cols() { return g_cols; }
uint32_t term_rows() { return g_rows; }

Window* win_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title) {
    if (g_nwindows >= (int)MAX_WINDOWS) return nullptr;

    int slot = -1;
    for (int i = 0; i < (int)MAX_WINDOWS; ++i) {
        if (!g_windows[i].visible) { slot = i; break; }
    }
    if (slot < 0) return nullptr;

    Window& win = g_windows[slot];
    win.x = x;
    win.y = y;
    win.w = w;
    win.h = h;
    win.client_h = (h > WIN_TITLEBAR_H) ? (h - WIN_TITLEBAR_H) : 0;
    win.fb_w         = w;
    win.fb_client_h  = win.client_h;
    win.dirty    = true;
    win.visible  = true;

    uint32_t ti = 0;
    while (title[ti] && ti < 31) { win.title[ti] = title[ti]; ++ti; }
    win.title[ti] = '\0';

    uint32_t npix = w * win.client_h;
    win.client_fb = nullptr;
    if (npix > 0) {
        win.client_fb = static_cast<uint32_t*>(kheap::alloc(npix * 4, 16));
        if (win.client_fb) {
            for (uint32_t i = 0; i < npix; ++i)
                win.client_fb[i] = COL_WIN_CLIENT_BG;
        }
    }

    g_zorder[g_nwindows] = (uint8_t)slot;
    ++g_nwindows;

    g_desktop_dirty = true;
    printk("wm: created window '%s' at (%d,%d) %ux%u\n",
           win.title, x, y, w, h);
    return &win;
}

void win_destroy(Window* win) {
    if (!win) return;
    int slot = (int)(win - g_windows);
    if (slot < 0 || slot >= (int)MAX_WINDOWS) return;

    if (win->client_fb) kheap::free(win->client_fb);

    int pos = -1;
    for (int i = 0; i < g_nwindows; ++i) {
        if (g_zorder[i] == (uint8_t)slot) { pos = i; break; }
    }
    if (pos >= 0) {
        for (int i = pos; i < g_nwindows - 1; ++i)
            g_zorder[i] = g_zorder[i + 1];
        --g_nwindows;
    }

    *win = Window{};
    g_desktop_dirty = true;
}

void win_mark_dirty(Window* win) {
    if (win) win->dirty = true;
}

void mouse_update(int32_t abs_x, int32_t abs_y, bool btn_left, bool btn_right) {

    if (abs_x < 0)               abs_x = 0;
    if (abs_y < 0)               abs_y = 0;
    if ((uint32_t)abs_x >= g_sw) abs_x = (int32_t)(g_sw - 1);
    if ((uint32_t)abs_y >= g_sh) abs_y = (int32_t)(g_sh - 1);

    if (abs_x != cursor::pos_x() || abs_y != cursor::pos_y())
        g_cursor_dirty = true;

    cursor::set_pos(abs_x, abs_y);

    bool pressed  = (btn_left && !g_prev_btn);
    bool released = (!btn_left && g_prev_btn);
    g_prev_btn = btn_left;

    bool right_released = (!btn_right && g_prev_btn_right);
    g_prev_btn_right = btn_right;

    for (int i = 0; i < g_nwindows; ++i)
        g_windows[i].client_held = false;
    if (btn_left && !g_dragging) {
        int hold_wi = hit_test_client(abs_x, abs_y);
        if (hold_wi >= 0) {
            Window& hw = g_windows[hold_wi];
            hw.client_held = true;
            hw.held_cx = abs_x - hw.x;
            hw.held_cy = abs_y - (hw.y + (int32_t)WIN_TITLEBAR_H);
            g_desktop_dirty = true;
        }
    }

    if (right_released) {
        int client_wi = hit_test_client(abs_x, abs_y);
        if (client_wi >= 0) {
            Window& w = g_windows[client_wi];
            w.right_clicked = true;
            w.right_cx = abs_x - w.x;
            w.right_cy = abs_y - (w.y + (int32_t)WIN_TITLEBAR_H);
            g_desktop_dirty = true;
        }
    }

    if (pressed) {

        int close_wi = hit_test_close_btn(abs_x, abs_y);
        int max_wi   = hit_test_max_btn(abs_x, abs_y);
        if (close_wi < 0 && max_wi < 0) {

            int wi = hit_test_titlebar(abs_x, abs_y);
            if (wi >= 0) {
                zorder_bring_front(wi);
                g_desktop_dirty = true;
                if (!g_windows[wi].maximized) {
                    g_drag_win   = wi;
                    g_drag_off_x = abs_x - g_windows[wi].x;
                    g_drag_off_y = abs_y - g_windows[wi].y;
                    g_dragging   = true;
                }
            }
        }
    }

    if (released) {
        bool was_dragging = g_dragging;
        g_dragging = false;
        g_drag_win = -1;

        bool start_was_open = g_start_open;
        if (g_start_open) {
            uint32_t mh       = SM_HEADER_H + (uint32_t)SM_DEFAULT_COUNT * SM_ITEM_H + SM_DIVIDER_H;
            uint32_t menu_top = g_sh - TASKBAR_H - mh;

            if (g_sm_all_progs) {
                int      total        = g_sm_filtered_count;
                bool     needs_scroll = total > SM_AP_PAGE;
                int      visible      = (total > 0) ? (needs_scroll ? SM_AP_PAGE : total) : 1;
                uint32_t list_h       = (uint32_t)visible * SM_AP_ITEM_H;
                uint32_t aph          = SM_HEADER_H + SM_SEARCH_H + list_h
                                        + (needs_scroll ? 2u * SM_ARROW_H : 0u);
                uint32_t ap_top = g_sh - TASKBAR_H - aph;
                if ((uint32_t)abs_x >= SM_MENU_W &&
                    (uint32_t)abs_x <  SM_MENU_W + SM_ALL_W &&
                    (uint32_t)abs_y >= ap_top &&
                    (uint32_t)abs_y <  g_sh - TASKBAR_H) {
                    int32_t ry = abs_y - (int32_t)(ap_top + SM_HEADER_H);
                    if (ry >= 0) {

                        if (ry < (int32_t)SM_SEARCH_H) {
                            return;
                        }
                        ry -= (int32_t)SM_SEARCH_H;
                        if (needs_scroll) {

                            if (ry < (int32_t)SM_ARROW_H) {
                                if (g_sm_ap_scroll > 0) {
                                    --g_sm_ap_scroll;
                                    g_desktop_dirty = true;
                                }
                                return;
                            }
                            ry -= (int32_t)SM_ARROW_H;

                            if (ry >= (int32_t)list_h) {
                                int max_scroll = total - visible;
                                if (g_sm_ap_scroll < max_scroll) {
                                    ++g_sm_ap_scroll;
                                    g_desktop_dirty = true;
                                }
                                return;
                            }
                        }
                        if (total > 0) {
                            uint32_t row = (uint32_t)ry / SM_AP_ITEM_H;
                            if (row < (uint32_t)visible) {
                                int src = g_sm_filtered[g_sm_ap_scroll + (int)row];
                                g_start_sel = k_sm_allprog[src].dispatch;
                            }
                        }
                    }
                    g_start_open    = false;
                    g_sm_all_progs  = false;
                    g_sm_ap_scroll  = 0;
                    g_sm_search_len = 0;
                    g_sm_search[0]  = '\0';
                    g_desktop_dirty = true;
                    return;
                }
            }

            if ((uint32_t)abs_x < SM_MENU_W &&
                (uint32_t)abs_y >= menu_top &&
                (uint32_t)abs_y <  g_sh - TASKBAR_H) {
                int32_t ry = abs_y - (int32_t)(menu_top + SM_HEADER_H);
                int sel = -1;
                if (ry >= 0) {
                    uint32_t iy_acc = 0;
                    for (int i = 0; i < SM_DEFAULT_COUNT; ++i) {
                        if (i == SM_DEFAULT_DIV) iy_acc += SM_DIVIDER_H;
                        if (ry >= (int32_t)iy_acc &&
                            ry <  (int32_t)(iy_acc + SM_ITEM_H)) {
                            sel = i;
                            break;
                        }
                        iy_acc += SM_ITEM_H;
                    }
                }
                if (sel >= 0) {
                    int disp = k_sm_default[sel].dispatch;
                    if (disp == -2) {

                        g_sm_all_progs = !g_sm_all_progs;
                        if (g_sm_all_progs) {
                            sm_rebuild_filter();
                        } else {
                            g_sm_ap_scroll  = 0;
                            g_sm_search_len = 0;
                            g_sm_search[0]  = '\0';
                        }
                        g_desktop_dirty = true;
                        return;
                    } else {
                        g_start_sel     = disp;
                        g_start_open    = false;
                        g_sm_all_progs  = false;
                        g_sm_ap_scroll  = 0;
                        g_sm_search_len = 0;
                        g_sm_search[0]  = '\0';
                        g_desktop_dirty = true;
                        return;
                    }
                }
                g_start_open    = false;
                g_sm_all_progs  = false;
                g_sm_ap_scroll  = 0;
                g_sm_search_len = 0;
                g_sm_search[0]  = '\0';
                g_desktop_dirty = true;
                return;
            }

            g_start_open    = false;
            g_sm_all_progs  = false;
            g_sm_ap_scroll  = 0;
            g_sm_search_len = 0;
            g_sm_search[0]  = '\0';
            g_desktop_dirty = true;
        }

        if (!was_dragging) {

            int close_wi = hit_test_close_btn(abs_x, abs_y);
            int max_wi   = hit_test_max_btn(abs_x, abs_y);
            if (max_wi >= 0) {
                win_toggle_maximize(max_wi);
            } else if (close_wi >= 0) {

                g_windows[close_wi].close_requested = true;
                g_desktop_dirty = true;
            } else {
                int client_wi = hit_test_client(abs_x, abs_y);
                if (client_wi >= 0) {

                    Window& w = g_windows[client_wi];
                    w.client_clicked = true;
                    w.click_cx = abs_x - w.x;
                    w.click_cy = abs_y - (w.y + (int32_t)WIN_TITLEBAR_H);
                    g_desktop_dirty = true;
                } else if ((uint32_t)abs_y >= g_sh - TASKBAR_H) {

                    if (!start_was_open &&
                        abs_x >= 4 && (uint32_t)abs_x < 4u + START_BTN_W) {

                        g_start_open = true;
                        g_desktop_dirty = true;
                    } else if ((uint32_t)abs_x >= 4u + START_BTN_W) {
                        handle_taskbar_click(abs_x);
                    }
                } else if (!hit_test_any_window(abs_x, abs_y) &&
                           (uint32_t)abs_y > WM_TITLEBAR_H) {

                    g_dbclick   = true;
                    g_dbclick_x = abs_x;
                    g_dbclick_y = abs_y;
                }
            }
        }
    }

    if (g_dragging && g_drag_win >= 0 && btn_left) {
        Window& w = g_windows[g_drag_win];
        int32_t new_x = abs_x - g_drag_off_x;
        int32_t new_y = abs_y - g_drag_off_y;

        if (new_x < 0)               new_x = 0;
        if (new_y < (int32_t)WM_TITLEBAR_H) new_y = (int32_t)WM_TITLEBAR_H;
        if (new_x + (int32_t)w.w > (int32_t)g_sw) new_x = (int32_t)g_sw - (int32_t)w.w;
        if (new_y + (int32_t)w.h > (int32_t)g_sh) new_y = (int32_t)g_sh - (int32_t)w.h;
        if (w.x != new_x || w.y != new_y) {
            w.x = new_x;
            w.y = new_y;
            g_desktop_dirty = true;
        }
    }
}

void set_wallpaper_color(uint32_t bgra) {
    g_wallpaper_color = bgra;
    g_desktop_dirty   = true;
    g_all_dirty       = true;
}

uint32_t get_wallpaper_color() { return g_wallpaper_color; }

void set_terminal_visible(bool visible) {
    if (g_term_visible == visible) return;
    g_term_visible  = visible;
    g_desktop_dirty = true;
    g_all_dirty     = true;
}

void term_render_to_fb(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t bg) {

    uint32_t n = dst_w * dst_h;
    for (uint32_t i = 0; i < n; ++i) dst[i] = bg;

    uint32_t vis_cols = dst_w / gfx::FONT_W;
    uint32_t vis_rows = dst_h / gfx::FONT_H;
    if (vis_cols > g_cols) vis_cols = g_cols;
    if (vis_rows > g_rows) vis_rows = g_rows;

    uint32_t start_row = (g_cur_row + 1 > vis_rows)
                         ? (g_cur_row + 1 - vis_rows) : 0u;

    for (uint32_t vrow = 0; vrow < vis_rows; ++vrow) {
        uint32_t row = start_row + vrow;
        if (row >= g_rows) break;
        for (uint32_t col = 0; col < vis_cols; ++col) {
            char c = g_cell[row][col];
            bool cur = (row == g_cur_row && col == g_cur_col);
            if (!cur && (!c || c == ' ')) continue;
            if (!c) c = ' ';
            uint32_t fg  = cur ? COL_CURSOR_FG : COL_TERM_FG;
            uint32_t bgc = cur ? COL_CURSOR_BG : bg;
            gfx::draw_char_into(dst, dst_w,
                                col * gfx::FONT_W, vrow * gfx::FONT_H,
                                c, fg, bgc);
        }
    }
}

int win_count() { return g_nwindows; }

Window* win_get(int idx) {
    if (idx < 0 || idx >= g_nwindows) return nullptr;
    int wi = g_zorder[idx];
    return (g_windows[wi].visible) ? &g_windows[wi] : nullptr;
}

bool desktop_was_clicked() {
    bool b = g_dbclick;
    g_dbclick = false;
    return b;
}
int32_t desktop_click_x() { return g_dbclick_x; }
int32_t desktop_click_y() { return g_dbclick_y; }

void term_set_cursor(uint32_t col, uint32_t row) {
    if (col < g_cols) g_cur_col = col;
    if (row < g_rows) g_cur_row = row;
}

bool start_app_was_selected(int& app_idx) {
    if (g_start_sel < 0) return false;
    app_idx     = g_start_sel;
    g_start_sel = -1;
    return true;
}

bool start_menu_wants_keys() {
    return g_start_open && g_sm_all_progs;
}

void start_menu_on_key(char c) {
    if (c == '\b' || c == 127) {
        if (g_sm_search_len > 0) {
            --g_sm_search_len;
            g_sm_search[g_sm_search_len] = '\0';
            sm_rebuild_filter();
            g_desktop_dirty = true;
        }
    } else if ((uint8_t)c >= 0x20u && g_sm_search_len < 30) {
        g_sm_search[g_sm_search_len++] = c;
        g_sm_search[g_sm_search_len]   = '\0';
        sm_rebuild_filter();
        g_desktop_dirty = true;
    }
}

}
