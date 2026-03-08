/*
  controlpanel.cpp - settings window with multiple sections
  shows the live clock, lets you pick a wallpaper color from swatches,
  lists open windows, has per-app window size settings, and a shutdown button
*/
#include "kernel/apps/controlpanel.hpp"
#include "kernel/core/rtc.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/core/print.hpp"
#include "kernel/fs/blkfs.hpp"
#include <stdint.h>
#include <string.h>

static constexpr uint32_t CP_W   = 520u;
static constexpr uint32_t CP_H   = 400u;
static constexpr uint32_t CP_FW  = CP_W;
static constexpr uint32_t CP_CH  = CP_H - wm::WIN_TITLEBAR_H;

static constexpr uint32_t Y_CLOCK_HDR  =   4u;
static constexpr uint32_t Y_CLOCK_TXT  =  24u;
static constexpr uint32_t Y_WP_HDR     =  48u;
static constexpr uint32_t Y_WP_SWATCH  =  68u;
static constexpr uint32_t Y_APP_HDR    = 126u;
static constexpr uint32_t Y_APP_LIST   = 144u;
static constexpr uint32_t APP_ROW_H    =  28u;
static constexpr uint32_t APP_COUNT    =   6u;
static constexpr uint32_t Y_SYS_HDR   = 318u;
static constexpr uint32_t Y_SHUTDOWN  = 338u;

static constexpr uint32_t Y_AS_BACK    =   6u;
static constexpr uint32_t Y_AS_SECT    =  34u;
static constexpr uint32_t Y_AS_WIDTH   =  58u;
static constexpr uint32_t Y_AS_HEIGHT  =  86u;
static constexpr uint32_t Y_AS_HINT    = 118u;

static constexpr uint32_t C_BG         = 0x00C0C0C0u;
static constexpr uint32_t C_SECT_BG    = 0x00808080u;
static constexpr uint32_t C_SECT_FG    = 0x00FFFFFFu;
static constexpr uint32_t C_TEXT       = 0x00000000u;
static constexpr uint32_t C_BTN        = 0x00C0C0C0u;
static constexpr uint32_t C_BTN_SEL    = 0x00000080u;
static constexpr uint32_t C_BTN_SEL_FG = 0x00FFFFFFu;
static constexpr uint32_t C_BTN_FG     = 0x00000000u;
static constexpr uint32_t C_HI         = 0x00FFFFFFu;
static constexpr uint32_t C_SHADOW     = 0x00404040u;

static constexpr uint32_t WP_PRESET_COUNT = 5;
static constexpr uint32_t wp_colors[WP_PRESET_COUNT] = {
    0x00008080u,
    0x00000080u,
    0x00008000u,
    0x00800000u,
    0x00646464u,
};
static const char* const wp_names[WP_PRESET_COUNT] = {
    "Teal", "Navy", "Forest", "Maroon", "Slate"
};

namespace {

static wm::Window* g_win    = nullptr;
static bool        g_active = false;
static bool        g_dirty  = true;

enum class CPPage { Main, AppSettings };
static CPPage  g_page         = CPPage::Main;
static int     g_settings_app = 0;

static uint32_t g_app_w[6] = { 640u, 720u, 520u, 360u, 480u, 480u };
static uint32_t g_app_h[6] = { 420u, 500u, 400u, 480u, 390u, 346u };
static constexpr uint32_t APP_W_MIN    = 320u;
static constexpr uint32_t APP_W_MAX    = 960u;
static constexpr uint32_t APP_H_MIN    = 200u;
static constexpr uint32_t APP_H_MAX    = 640u;
static constexpr uint32_t APP_SIZE_STEP =  32u;

static void fb_fill(uint32_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t col) {
    for (uint32_t dy = 0; dy < h && (y + dy) < CP_CH; ++dy)
        for (uint32_t dx = 0; dx < w && (x + dx) < CP_FW; ++dx)
            fb[(y + dy) * CP_FW + (x + dx)] = col;
}

static void fb_text(uint32_t* fb, uint32_t x, uint32_t y, const char* s,
                    uint32_t fg, uint32_t bg) {
    uint32_t cx = x;
    for (const char* p = s; *p; ++p, cx += gfx::FONT_W)
        gfx::draw_char_into(fb, CP_FW, cx, y, *p, fg, bg);
}

[[maybe_unused]]
static void fb_num2(uint32_t* fb, uint32_t x, uint32_t y,
                    uint8_t val, uint32_t fg, uint32_t bg) {
    char tmp[3] = { (char)('0' + val / 10), (char)('0' + val % 10), '\0' };
    fb_text(fb, x, y, tmp, fg, bg);
}

static void fb_uint(uint32_t* fb, uint32_t x, uint32_t y,
                    uint32_t val, uint32_t fg, uint32_t bg) {
    char tmp[12]; int n = 0;
    if (val == 0) { tmp[n++] = '0'; }
    else { uint32_t v = val; while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; } }
    for (int a = 0, b = n - 1; a < b; ++a, --b) { char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t; }
    tmp[n] = '\0';
    fb_text(fb, x, y, tmp, fg, bg);
}

static void fb_button(uint32_t* fb,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const char* label,
                      bool pressed) {
    uint32_t fill = pressed ? C_BTN_SEL : C_BTN;
    uint32_t fg   = pressed ? C_BTN_SEL_FG : C_BTN_FG;
    fb_fill(fb, x, y, w, h, fill);

    for (uint32_t i = 0; i < w; ++i) {
        fb[(y)         * CP_FW + x + i] = pressed ? C_SHADOW : C_HI;
        fb[(y + h - 1) * CP_FW + x + i] = pressed ? C_HI     : C_SHADOW;
    }
    for (uint32_t i = 1; i + 1 < h; ++i) {
        fb[(y + i) * CP_FW + x]         = pressed ? C_SHADOW : C_HI;
        fb[(y + i) * CP_FW + x + w - 1] = pressed ? C_HI     : C_SHADOW;
    }

    uint32_t llen = 0;
    while (label[llen]) ++llen;
    uint32_t lx = x + 2 + (w > llen * gfx::FONT_W + 4
                            ? (w - llen * gfx::FONT_W) / 2 : 2u);
    uint32_t ly = y + (h > gfx::FONT_H ? (h - gfx::FONT_H) / 2 : 0u);
    fb_text(fb, lx, ly, label, fg, fill);
}

static void fb_section(uint32_t* fb, uint32_t y, const char* label) {
    fb_fill(fb, 0, y, CP_FW, gfx::FONT_H + 2u, C_SECT_BG);
    fb_text(fb, 4, y + 1, label, C_SECT_FG, C_SECT_BG);
}

static void fb_swatch(uint32_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color, bool selected) {
    fb_fill(fb, x, y, w, h, color);

    static constexpr uint32_t C_BLACK = 0x00000000u;
    for (uint32_t i = 0; i < w; ++i) {
        fb[y             * CP_FW + x + i] = C_BLACK;
        fb[(y + h - 1)   * CP_FW + x + i] = C_BLACK;
    }
    for (uint32_t i = 1; i + 1 < h; ++i) {
        fb[(y + i) * CP_FW + x]           = C_BLACK;
        fb[(y + i) * CP_FW + x + w - 1]   = C_BLACK;
    }
    if (selected) {

        for (uint32_t i = 1; i < w - 1; ++i) {
            fb[(y + 1)     * CP_FW + x + i] = C_HI;
            fb[(y + h - 2) * CP_FW + x + i] = C_HI;
        }
        for (uint32_t i = 2; i + 2 < h; ++i) {
            fb[(y + i) * CP_FW + x + 1]     = C_HI;
            fb[(y + i) * CP_FW + x + w - 2] = C_HI;
        }
    }
}

static bool in_rect(int32_t cx, int32_t cy, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

static void draw_app_settings(uint32_t* fb) {
    fb_fill(fb, 0u, 0u, CP_FW, CP_CH, C_BG);

    fb_button(fb, 4u, Y_AS_BACK, 64u, 22u, "< Back", false);

    static const char* const app_names[6] = {
        " Shell Settings", " Text Editor Settings",
        " Control Panel Settings", " Calculator Settings",
        " File Explorer Settings", " Paint Settings"
    };
    fb_section(fb, Y_AS_SECT, app_names[g_settings_app]);

    fb_text(fb, 8u, Y_AS_WIDTH + 4u, "Window Width:", C_TEXT, C_BG);
    fb_fill(fb, 130u, Y_AS_WIDTH, 80u, 22u, C_BG);
    fb_uint(fb, 130u, Y_AS_WIDTH + 4u, g_app_w[g_settings_app], C_TEXT, C_BG);
    fb_button(fb, 224u, Y_AS_WIDTH, 26u, 22u, "-", false);
    fb_button(fb, 254u, Y_AS_WIDTH, 26u, 22u, "+", false);

    fb_text(fb, 8u, Y_AS_HEIGHT + 4u, "Window Height:", C_TEXT, C_BG);
    fb_fill(fb, 130u, Y_AS_HEIGHT, 80u, 22u, C_BG);
    fb_uint(fb, 130u, Y_AS_HEIGHT + 4u, g_app_h[g_settings_app], C_TEXT, C_BG);
    fb_button(fb, 224u, Y_AS_HEIGHT, 26u, 22u, "-", false);
    fb_button(fb, 254u, Y_AS_HEIGHT, 26u, 22u, "+", false);

    fb_text(fb, 8u, Y_AS_HINT, "Reopen window to apply changes.", C_SHADOW, C_BG);
}

static void handle_click_app_settings(int32_t cx, int32_t cy) {

    if (in_rect(cx, cy, 4u, Y_AS_BACK, 64u, 22u)) {
        g_page  = CPPage::Main;
        g_dirty = true;
        return;
    }
    int ai = g_settings_app;

    if (in_rect(cx, cy, 224u, Y_AS_WIDTH, 26u, 22u)) {
        g_app_w[ai] = (g_app_w[ai] > APP_W_MIN + APP_SIZE_STEP - 1u)
                      ? g_app_w[ai] - APP_SIZE_STEP : APP_W_MIN;
        g_dirty = true; return;
    }

    if (in_rect(cx, cy, 254u, Y_AS_WIDTH, 26u, 22u)) {
        g_app_w[ai] = (g_app_w[ai] + APP_SIZE_STEP <= APP_W_MAX)
                      ? g_app_w[ai] + APP_SIZE_STEP : APP_W_MAX;
        g_dirty = true; return;
    }

    if (in_rect(cx, cy, 224u, Y_AS_HEIGHT, 26u, 22u)) {
        g_app_h[ai] = (g_app_h[ai] > APP_H_MIN + APP_SIZE_STEP - 1u)
                      ? g_app_h[ai] - APP_SIZE_STEP : APP_H_MIN;
        g_dirty = true; return;
    }

    if (in_rect(cx, cy, 254u, Y_AS_HEIGHT, 26u, 22u)) {
        g_app_h[ai] = (g_app_h[ai] + APP_SIZE_STEP <= APP_H_MAX)
                      ? g_app_h[ai] + APP_SIZE_STEP : APP_H_MAX;
        g_dirty = true; return;
    }
}

static const char* wday_name(uint8_t wd) {
    static const char* names[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return (wd < 7) ? names[wd] : "???";
}
static const char* month_name(uint8_t mo) {
    static const char* names[13] = {"","Jan","Feb","Mar","Apr","May","Jun",
                                       "Jul","Aug","Sep","Oct","Nov","Dec"};
    return (mo >= 1 && mo <= 12) ? names[mo] : "???";
}

static void draw_full(uint32_t* fb, uint64_t ticks) {
    if (g_page == CPPage::AppSettings) { draw_app_settings(fb); return; }

    fb_fill(fb, 0, 0, CP_FW, CP_CH, C_BG);

    fb_section(fb, Y_CLOCK_HDR, " Clock");
    {
        rtc::DateTime dt = rtc::now(ticks);

        static char line[48];
        int i = 0;
        const char* day_s  = wday_name(dt.wday);
        const char* mon_s  = month_name(dt.month);
        line[i++] = day_s[0]; line[i++] = day_s[1]; line[i++] = day_s[2]; line[i++] = ' ';
        line[i++] = (char)('0' + dt.day / 10); line[i++] = (char)('0' + dt.day % 10); line[i++] = ' ';
        line[i++] = mon_s[0]; line[i++] = mon_s[1]; line[i++] = mon_s[2]; line[i++] = ' ';
        line[i++] = (char)('0' + dt.year / 1000);
        line[i++] = (char)('0' + (dt.year / 100) % 10);
        line[i++] = (char)('0' + (dt.year / 10 ) % 10);
        line[i++] = (char)('0' + dt.year % 10);
        line[i++] = ' '; line[i++] = ' ';
        line[i++] = (char)('0' + dt.hour / 10); line[i++] = (char)('0' + dt.hour % 10); line[i++] = ':';
        line[i++] = (char)('0' + dt.min  / 10); line[i++] = (char)('0' + dt.min  % 10); line[i++] = ':';
        line[i++] = (char)('0' + dt.sec  / 10); line[i++] = (char)('0' + dt.sec  % 10); line[i++] = ' ';
        const char* tz = rtc::tz_name();
        while (*tz) line[i++] = *tz++;
        line[i] = '\0';
        fb_text(fb, 4, Y_CLOCK_TXT, line, C_TEXT, C_BG);
    }

    fb_section(fb, Y_WP_HDR, " Wallpaper Color");
    {
        static constexpr uint32_t SW_W   = 80u;
        static constexpr uint32_t SW_H   = 36u;
        static constexpr uint32_t SW_GAP =  8u;
        uint32_t cur_wp = wm::get_wallpaper_color();
        for (uint32_t i = 0; i < WP_PRESET_COUNT; ++i) {
            uint32_t sx = 8u + i * (SW_W + SW_GAP);
            fb_swatch(fb, sx, Y_WP_SWATCH, SW_W, SW_H,
                      wp_colors[i], wp_colors[i] == cur_wp);
            uint32_t llen = 0; while (wp_names[i][llen]) ++llen;
            uint32_t lx = sx + (llen * gfx::FONT_W < SW_W ?
                                 (SW_W - llen * gfx::FONT_W) / 2 : 0u);
            fb_text(fb, lx, Y_WP_SWATCH + SW_H + 2u, wp_names[i], C_TEXT, C_BG);
        }
    }

    fb_section(fb, Y_APP_HDR, " Apps");
    {
        static const char* const app_labels[APP_COUNT] = {
            "Shell", "Text Editor", "Control Panel", "Calculator", "File Explorer", "Paint"
        };
        static constexpr uint32_t SETTINGS_BTN_W = 86u;
        for (uint32_t i = 0; i < APP_COUNT; ++i) {
            uint32_t ry     = Y_APP_LIST + i * APP_ROW_H;
            uint32_t row_bg = (i % 2 == 0) ? C_BG : 0x00B0B0B0u;
            fb_fill(fb, 0u, ry, CP_FW, APP_ROW_H, row_bg);
            fb_text(fb, 8u, ry + 6u, app_labels[i], C_TEXT, row_bg);
            uint32_t bx = CP_FW - 8u - SETTINGS_BTN_W;
            fb_button(fb, bx, ry + 4u, SETTINGS_BTN_W, APP_ROW_H - 8u, "Settings >", false);
        }
    }

    fb_section(fb, Y_SYS_HDR, " System");
    {
        static constexpr uint32_t SD_W = 160u;
        static constexpr uint32_t SD_H =  24u;
        uint32_t sx = (CP_FW - SD_W) / 2u;
        fb_button(fb, sx, Y_SHUTDOWN, SD_W, SD_H, "Shut Down", false);
    }
}

static bool in_rect(int32_t cx, int32_t cy,
                    uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return cx >= (int32_t)x && cx < (int32_t)(x + w) &&
           cy >= (int32_t)y && cy < (int32_t)(y + h);
}

static void handle_click(int32_t cx, int32_t cy, uint32_t* ) {
    if (g_page == CPPage::AppSettings) { handle_click_app_settings(cx, cy); return; }

    static constexpr uint32_t SW_W   = 80u;
    static constexpr uint32_t SW_H   = 36u;
    static constexpr uint32_t SW_GAP =  8u;
    for (uint32_t i = 0; i < WP_PRESET_COUNT; ++i) {
        uint32_t sx = 8u + i * (SW_W + SW_GAP);
        if (in_rect(cx, cy, sx, Y_WP_SWATCH, SW_W, SW_H)) {
            wm::set_wallpaper_color(wp_colors[i]);
            g_dirty = true;
            return;
        }
    }

    static constexpr uint32_t SETTINGS_BTN_W = 86u;
    for (uint32_t i = 0; i < APP_COUNT; ++i) {
        uint32_t ry = Y_APP_LIST + i * APP_ROW_H;
        uint32_t bx = CP_FW - 8u - SETTINGS_BTN_W;
        if (in_rect(cx, cy, bx, ry + 4u, SETTINGS_BTN_W, APP_ROW_H - 8u)) {
            g_settings_app = (int)i;
            g_page  = CPPage::AppSettings;
            g_dirty = true;
            return;
        }
    }

    static constexpr uint32_t SD_W = 160u;
    static constexpr uint32_t SD_H =  24u;
    uint32_t sx = (CP_FW - SD_W) / 2u;
    if (in_rect(cx, cy, sx, Y_SHUTDOWN, SD_W, SD_H)) {
        blkfs::flush();
        register uint64_t x0 asm("x0") = 0x84000008ULL;
        asm volatile("hvc #0" :: "r"(x0) : "memory");
        for (;;) asm volatile("wfi");
    }
}

}

namespace controlpanel {

void open() {
    if (g_active) return;

    int32_t ox = (int32_t)((1280u - CP_W) / 2u);
    int32_t oy = (int32_t)((800u  - CP_H) / 2u);
    g_win = wm::win_create(ox, oy, CP_W, CP_H, "Control Panel");
    if (!g_win) return;
    g_active = true;
    g_dirty  = true;
}

void close() {
    if (!g_active) return;
    wm::win_destroy(g_win);
    g_win    = nullptr;
    g_active = false;
}

bool active() { return g_active; }

void tick(uint64_t ticks) {
    if (!g_active || !g_win) return;

    if (g_win->close_requested) { close(); return; }

    uint32_t* fb = g_win->client_fb;
    if (!fb) return;

    static uint64_t last_sec = 0;
    uint64_t cur_sec = ticks / 100u;
    bool need_redraw = g_dirty || (g_page == CPPage::Main && cur_sec != last_sec);

    if (g_win->client_clicked) {
        handle_click(g_win->click_cx, g_win->click_cy, fb);
        g_win->client_clicked = false;
        need_redraw = true;
    }

    if (need_redraw) {
        last_sec = cur_sec;
        g_dirty  = false;
        draw_full(fb, ticks);
        wm::win_mark_dirty(g_win);
    }
}

}

namespace controlpanel {
    uint32_t app_pref_w(int idx) {
        if (idx < 0 || idx >= 2) return 640u;
        return g_app_w[idx];
    }
    uint32_t app_pref_h(int idx) {
        if (idx < 0 || idx >= 2) return 420u;
        return g_app_h[idx];
    }
}
