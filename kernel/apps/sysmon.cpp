/*
  sysmon.cpp - task manager style system monitor app
  performance tab: rolling cpu chart (frames/sec), memory bar, uptime, fps
  processes tab: list of open windows with an end-task button
*/
#include "kernel/apps/sysmon.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/rtc.hpp"
#include <stdint.h>
#include <string.h>

static constexpr uint32_t SM_W  = 580u;
static constexpr uint32_t SM_CH = 440u;
static constexpr uint32_t SM_H  = SM_CH + wm::WIN_TITLEBAR_H;

static constexpr uint32_t C_BG       = 0x00C0C0C0u;
static constexpr uint32_t C_WHITE    = 0x00FFFFFFu;
static constexpr uint32_t C_SHADOW   = 0x00404040u;
static constexpr uint32_t C_HI       = 0x00FFFFFFu;
static constexpr uint32_t C_SECT     = 0x00000080u;
static constexpr uint32_t C_SECT_FG  = 0x00FFFFFFu;
static constexpr uint32_t C_TAB_ACT  = 0x00C0C0C0u;
static constexpr uint32_t C_TAB_INACT= 0x00A0A0A0u;
static constexpr uint32_t C_TAB_FG   = 0x00000000u;
static constexpr uint32_t C_CHART_BG = 0x00000000u;
static constexpr uint32_t C_CHART_GR = 0x0000AA00u;
static constexpr uint32_t C_CHART_FG = 0x0000FF00u;
static constexpr uint32_t C_MEM_BG   = 0x00000040u;
static constexpr uint32_t C_MEM_FG   = 0x000000FFu;
static constexpr uint32_t C_LIST_SEL = 0x00000080u;
static constexpr uint32_t C_LIST_ALT = 0x00D0D0D0u;
static constexpr uint32_t C_TEXT     = 0x00000000u;
static constexpr uint32_t C_BTN_REG  = 0x00C0C0C0u;
static constexpr uint32_t C_BTN_RED  = 0x008B0000u;
static constexpr uint32_t C_BTN_REDFG= 0x00FFFFFFu;

static constexpr uint32_t TAB_H    = 22u;
static constexpr uint32_t TAB_W    = 120u;
static constexpr uint32_t TAB_Y    = 2u;

static constexpr uint32_t CONTENT_Y = TAB_Y + TAB_H + 2u;
static constexpr uint32_t CONTENT_H = SM_CH - CONTENT_Y - 2u;
static constexpr uint32_t CONTENT_X = 4u;
static constexpr uint32_t CONTENT_W = SM_W - 8u;

static constexpr uint32_t PERF_PAD    = 6u;
static constexpr uint32_t CHART_BARS  = 48u;
static constexpr uint32_t CHART_W     = CONTENT_W - 2u * PERF_PAD;
static constexpr uint32_t CHART_H     = 100u;
static constexpr uint32_t CHART_X     = CONTENT_X + PERF_PAD;
static constexpr uint32_t CHART_Y     = CONTENT_Y + 22u;
static constexpr uint32_t MEM_BAR_W   = CONTENT_W - 2u * PERF_PAD;
static constexpr uint32_t MEM_BAR_H   = 20u;
static constexpr uint32_t MEM_BAR_X   = CONTENT_X + PERF_PAD;
static constexpr uint32_t MEM_BAR_Y   = CHART_Y + CHART_H + 30u;
static constexpr uint32_t INFO_Y      = MEM_BAR_Y + MEM_BAR_H + 8u;

static constexpr uint32_t PROC_LIST_Y  = CONTENT_Y + 20u;
static constexpr uint32_t PROC_ROW_H   = 18u;
static constexpr uint32_t PROC_VISIBLE = (CONTENT_H - 20u - 30u) / PROC_ROW_H;
static constexpr uint32_t PROC_COL1_W  = 260u;
static constexpr uint32_t PROC_BTN_W   = 100u;
static constexpr uint32_t PROC_BTN_H   = 22u;

namespace {

static wm::Window* g_win    = nullptr;
static bool        g_active = false;

static int g_tab = 0;

static constexpr uint32_t HISTORY_LEN = CHART_BARS;
static uint8_t  g_cpu_hist[HISTORY_LEN] = {};
static uint32_t g_hist_write = 0;

static uint32_t g_frame_counter  = 0;
static uint64_t g_last_sample_t  = 0;

static int g_proc_sel    = -1;
static int g_proc_scroll =  0;

static inline void fb_fill(uint32_t* fb, int32_t x, int32_t y,
                            uint32_t w, uint32_t h, uint32_t col) {
    for (uint32_t dy = 0; dy < h; ++dy) {
        int32_t py = y + (int32_t)dy;
        if (py < 0 || (uint32_t)py >= SM_CH) continue;
        for (uint32_t dx = 0; dx < w; ++dx) {
            int32_t px = x + (int32_t)dx;
            if (px < 0 || (uint32_t)px >= SM_W) continue;
            fb[(uint32_t)py * SM_W + (uint32_t)px] = col;
        }
    }
}

static inline void fb_text(uint32_t* fb, int32_t x, int32_t y, const char* s,
                            uint32_t fg, uint32_t bg) {
    int32_t cx = x;
    for (const char* p = s; *p; ++p, cx += (int32_t)gfx::FONT_W)
        gfx::draw_char_into(fb, SM_W, (uint32_t)cx, (uint32_t)y, *p, fg, bg);
}

static void fb_text_right(uint32_t* fb, int32_t x, int32_t y,
                           uint32_t field_w, const char* s,
                           uint32_t fg, uint32_t bg) {
    uint32_t len = 0;
    while (s[len]) ++len;
    uint32_t tw = len * gfx::FONT_W;
    int32_t rx = tw <= field_w ? (x + (int32_t)(field_w - tw)) : x;
    fb_text(fb, rx, y, s, fg, bg);
}

static void fb_button(uint32_t* fb, int32_t x, int32_t y,
                      uint32_t w, uint32_t h, const char* label,
                      uint32_t fill, uint32_t fg) {
    fb_fill(fb, x, y, w, h, fill);
    for (uint32_t i = 0; i < w; ++i) {
        if ((uint32_t)y < SM_CH)          fb[(uint32_t)y * SM_W + (uint32_t)(x + (int32_t)i)]          = C_HI;
        if ((uint32_t)(y+h-1) < SM_CH)    fb[(uint32_t)(y+h-1) * SM_W + (uint32_t)(x + (int32_t)i)]   = C_SHADOW;
    }
    for (uint32_t i = 1; i + 1 < h; ++i) {
        uint32_t row = (uint32_t)(y + (int32_t)i);
        if (row < SM_CH) {
            fb[row * SM_W + (uint32_t)x]         = C_HI;
            fb[row * SM_W + (uint32_t)(x+w-1)]   = C_SHADOW;
        }
    }
    uint32_t llen = 0;
    while (label[llen]) ++llen;
    int32_t lx = x + 2 + (int32_t)((w > llen * gfx::FONT_W + 4) ? (w - llen * gfx::FONT_W) / 2 : 2u);
    int32_t ly = y + (int32_t)((h > gfx::FONT_H) ? (h - gfx::FONT_H) / 2 : 0u);
    fb_text(fb, lx, ly, label, fg, fill);
}

static char* fmt_uint_rev(char* end, uint32_t v) {
    if (v == 0) { *--end = '0'; return end; }
    while (v) { *--end = (char)('0' + v % 10); v /= 10; }
    return end;
}

static void uint_to_str(uint32_t v, char* buf) {
    char tmp[12]; tmp[11] = '\0';
    char* p = fmt_uint_rev(tmp + 11, v);
    uint32_t i = 0;
    while (*p) buf[i++] = *p++;
    buf[i] = '\0';
}

static void fmt_mib(char* buf, uint32_t bytes) {
    uint32_t mib = bytes / (1024u * 1024u);
    char tmp[8]; uint_to_str(mib, tmp);
    uint32_t i = 0;
    const char* p = tmp;
    while (*p) buf[i++] = *p++;
    buf[i++] = ' '; buf[i++] = 'M'; buf[i++] = 'i'; buf[i++] = 'B'; buf[i] = '\0';
}

static void fmt_uptime(char* buf, uint64_t ticks_100hz) {
    uint64_t secs  = ticks_100hz / 100u;
    uint32_t days  = (uint32_t)(secs / 86400u);
    uint32_t hh    = (uint32_t)((secs % 86400u) / 3600u);
    uint32_t mm    = (uint32_t)((secs % 3600u) / 60u);
    uint32_t ss    = (uint32_t)(secs % 60u);
    uint32_t i = 0;
    if (days) {
        char d[8]; uint_to_str(days, d);
        for (const char* p = d; *p; ++p) buf[i++] = *p;
        buf[i++] = 'd'; buf[i++] = ' ';
    }
    buf[i++] = (char)('0' + hh / 10);
    buf[i++] = (char)('0' + hh % 10);
    buf[i++] = ':';
    buf[i++] = (char)('0' + mm / 10);
    buf[i++] = (char)('0' + mm % 10);
    buf[i++] = ':';
    buf[i++] = (char)('0' + ss / 10);
    buf[i++] = (char)('0' + ss % 10);
    buf[i]   = '\0';
}

static void draw_chrome(uint32_t* fb) {

    fb_fill(fb, 0, 0, SM_W, SM_CH, C_BG);

    const char* tab_labels[] = { "Performance", "Processes" };
    for (int t = 0; t < 2; ++t) {
        uint32_t tx = (uint32_t)t * (TAB_W + 2u) + 4u;
        bool active = (t == g_tab);
        uint32_t fill = active ? C_TAB_ACT : C_TAB_INACT;
        fb_fill(fb, (int32_t)tx, (int32_t)TAB_Y, TAB_W, TAB_H, fill);

        for (uint32_t i = 0; i < TAB_W; ++i) {
            fb[TAB_Y * SM_W + tx + i]             = active ? C_HI : C_SHADOW;
            fb[(TAB_Y + TAB_H - 1) * SM_W + tx + i] = active ? C_BG : C_SHADOW;
        }
        for (uint32_t i = 1; i + 1 < TAB_H; ++i) {
            fb[(TAB_Y + i) * SM_W + tx]           = active ? C_HI : C_SHADOW;
            fb[(TAB_Y + i) * SM_W + tx + TAB_W - 1] = C_SHADOW;
        }

        uint32_t llen = 0;
        while (tab_labels[t][llen]) ++llen;
        uint32_t lx = tx + (TAB_W - llen * gfx::FONT_W) / 2u;
        uint32_t ly = TAB_Y + (TAB_H - gfx::FONT_H) / 2u;
        fb_text(fb, (int32_t)lx, (int32_t)ly, tab_labels[t], C_TAB_FG, fill);
    }

    uint32_t by = CONTENT_Y - 1u;
    uint32_t bh = SM_CH - by - 2u;
    for (uint32_t i = 0; i < SM_W - 8u; ++i) {
        fb[by * SM_W + 4u + i]          = C_SHADOW;
        fb[(by + bh) * SM_W + 4u + i]   = C_HI;
    }
    for (uint32_t i = 1; i < bh; ++i) {
        fb[(by + i) * SM_W + 4u]            = C_SHADOW;
        fb[(by + i) * SM_W + SM_W - 5u]     = C_HI;
    }
}

static void draw_performance(uint32_t* fb, uint64_t ticks_100hz) {

    fb_fill(fb, (int32_t)CONTENT_X, (int32_t)CONTENT_Y, CONTENT_W, gfx::FONT_H + 4u, C_SECT);
    fb_text(fb, (int32_t)(CONTENT_X + 4u), (int32_t)(CONTENT_Y + 2u),
            "  CPU Usage History", C_SECT_FG, C_SECT);

    fb_fill(fb, (int32_t)CHART_X, (int32_t)CHART_Y, CHART_W, CHART_H, C_CHART_BG);

    static constexpr uint32_t k_grid_pcts[3] = { 25u, 50u, 75u };
    for (uint32_t gi = 0; gi < 3u; ++gi) {
        uint32_t pct = k_grid_pcts[gi];
        uint32_t gy = CHART_Y + CHART_H - 1u - (pct * (CHART_H - 2u) / 100u);
        for (uint32_t x = CHART_X; x < CHART_X + CHART_W; x += 4u)
            if (gy < SM_CH) fb[gy * SM_W + x] = C_CHART_GR;
    }

    uint32_t bar_w = CHART_W / HISTORY_LEN;
    if (bar_w < 1u) bar_w = 1u;
    for (uint32_t i = 0; i < HISTORY_LEN; ++i) {

        uint32_t idx = (g_hist_write + i) % HISTORY_LEN;
        uint32_t val = g_cpu_hist[idx];
        uint32_t bh  = (uint32_t)val * (CHART_H - 2u) / 100u;
        if (bh == 0 && val > 0) bh = 1u;
        uint32_t bx = CHART_X + i * bar_w;
        uint32_t by_bot = CHART_Y + CHART_H - 1u;
        fb_fill(fb, (int32_t)bx, (int32_t)(by_bot - bh), bar_w, bh, C_CHART_FG);
    }

    for (uint32_t i = 0; i < CHART_W; ++i) {
        fb[(CHART_Y - 1u)            * SM_W + CHART_X + i] = C_SHADOW;
        fb[(CHART_Y + CHART_H)       * SM_W + CHART_X + i] = C_HI;
    }
    for (uint32_t i = 0; i < CHART_H + 2u; ++i) {
        fb[(CHART_Y - 1u + i) * SM_W + CHART_X - 1u]         = C_SHADOW;
        fb[(CHART_Y - 1u + i) * SM_W + CHART_X + CHART_W]    = C_HI;
    }

    uint32_t mem_sect_y = MEM_BAR_Y - gfx::FONT_H - 6u;
    fb_fill(fb, (int32_t)CONTENT_X, (int32_t)mem_sect_y, CONTENT_W, gfx::FONT_H + 4u, C_SECT);
    fb_text(fb, (int32_t)(CONTENT_X + 4u), (int32_t)(mem_sect_y + 2u),
            "  Physical Memory Usage", C_SECT_FG, C_SECT);

    uint32_t total = (uint32_t)(kheap::used_bytes() + kheap::free_bytes());
    uint32_t used  = (uint32_t)kheap::used_bytes();
    uint32_t fill_w = total > 0 ? (used * MEM_BAR_W / total) : 0u;

    fb_fill(fb, (int32_t)MEM_BAR_X, (int32_t)MEM_BAR_Y, MEM_BAR_W, MEM_BAR_H, C_MEM_BG);
    if (fill_w > 0)
        fb_fill(fb, (int32_t)MEM_BAR_X, (int32_t)MEM_BAR_Y, fill_w, MEM_BAR_H, C_MEM_FG);

    for (uint32_t i = 0; i < MEM_BAR_W; ++i) {
        fb[(MEM_BAR_Y - 1u) * SM_W + MEM_BAR_X + i] = C_SHADOW;
        fb[(MEM_BAR_Y + MEM_BAR_H) * SM_W + MEM_BAR_X + i] = C_HI;
    }
    for (uint32_t i = 0; i < MEM_BAR_H + 2u; ++i) {
        fb[(MEM_BAR_Y - 1u + i) * SM_W + MEM_BAR_X - 1u]      = C_SHADOW;
        fb[(MEM_BAR_Y - 1u + i) * SM_W + MEM_BAR_X + MEM_BAR_W] = C_HI;
    }

    char buf[32];
    fmt_mib(buf, used);
    fb_text_right(fb, (int32_t)MEM_BAR_X, (int32_t)(MEM_BAR_Y - gfx::FONT_H - 2u),
                  MEM_BAR_W / 2u, buf, C_TEXT, C_BG);
    fb_text(fb, (int32_t)(MEM_BAR_X + MEM_BAR_W / 2u),
            (int32_t)(MEM_BAR_Y - gfx::FONT_H - 2u),
            " / ", C_TEXT, C_BG);
    fmt_mib(buf, total);
    fb_text(fb, (int32_t)(MEM_BAR_X + MEM_BAR_W / 2u + 3u * gfx::FONT_W),
            (int32_t)(MEM_BAR_Y - gfx::FONT_H - 2u),
            buf, C_TEXT, C_BG);

    uint32_t iy = INFO_Y;
    uint32_t col1 = CONTENT_X + PERF_PAD;
    uint32_t col2 = col1 + 180u;

    fb_text(fb, (int32_t)col1, (int32_t)iy, "Uptime:", C_TEXT, C_BG);
    fmt_uptime(buf, ticks_100hz);
    fb_text(fb, (int32_t)col2, (int32_t)iy, buf, C_TEXT, C_BG);
    iy += gfx::FONT_H + 4u;

    fb_text(fb, (int32_t)col1, (int32_t)iy, "Open Windows:", C_TEXT, C_BG);
    uint_to_str((uint32_t)wm::win_count(), buf);
    fb_text(fb, (int32_t)col2, (int32_t)iy, buf, C_TEXT, C_BG);
    iy += gfx::FONT_H + 4u;

    fb_text(fb, (int32_t)col1, (int32_t)iy, "Render FPS:", C_TEXT, C_BG);

    uint32_t fps_idx = g_hist_write == 0 ? HISTORY_LEN - 1u : g_hist_write - 1u;
    uint32_t fps_val = g_cpu_hist[fps_idx];
    uint_to_str(fps_val, buf);
    fb_text(fb, (int32_t)col2, (int32_t)iy, buf, C_TEXT, C_BG);
    iy += gfx::FONT_H + 4u;

    fb_text(fb, (int32_t)col1, (int32_t)iy, "Heap Free:", C_TEXT, C_BG);
    fmt_mib(buf, (uint32_t)kheap::free_bytes());
    fb_text(fb, (int32_t)col2, (int32_t)iy, buf, C_TEXT, C_BG);
}

static constexpr int MAX_PROCS = wm::MAX_WINDOWS + 1;

struct ProcEntry {
    char     name[36];
    bool     is_kernel;
    wm::Window* win;
};

static int build_proc_list(ProcEntry* out) {
    int n = 0;

    out[n].is_kernel = true;
    out[n].win       = nullptr;
    static const char k_kernel[] = "Kernel";
    for (int i = 0; i < 7; ++i) out[n].name[i] = k_kernel[i];
    ++n;

    int wc = wm::win_count();
    for (int i = 0; i < wc && n < MAX_PROCS; ++i) {
        wm::Window* w = wm::win_get(i);
        if (!w) continue;
        out[n].is_kernel = false;
        out[n].win       = w;
        uint32_t l = 0;
        while (w->title[l] && l < 35u) { out[n].name[l] = w->title[l]; ++l; }
        out[n].name[l] = '\0';
        ++n;
    }
    return n;
}

static void draw_processes(uint32_t* fb) {

    fb_fill(fb, (int32_t)CONTENT_X, (int32_t)CONTENT_Y, CONTENT_W, gfx::FONT_H + 4u, C_SECT);
    fb_text(fb, (int32_t)(CONTENT_X + 4u),   (int32_t)(CONTENT_Y + 2u), "Name",   C_SECT_FG, C_SECT);
    fb_text(fb, (int32_t)(CONTENT_X + PROC_COL1_W), (int32_t)(CONTENT_Y + 2u), "Status", C_SECT_FG, C_SECT);

    ProcEntry procs[MAX_PROCS];
    int nprocs = build_proc_list(procs);

    int max_scroll = nprocs - (int)PROC_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (g_proc_scroll > max_scroll) g_proc_scroll = max_scroll;

    for (uint32_t row = 0; row < PROC_VISIBLE; ++row) {
        int idx = (int)row + g_proc_scroll;
        uint32_t ry = PROC_LIST_Y + row * PROC_ROW_H;
        if (ry + PROC_ROW_H > SM_CH) break;

        uint32_t row_bg = (idx == g_proc_sel) ? C_LIST_SEL
                        : (row & 1u)           ? C_LIST_ALT
                                               : C_BG;
        uint32_t row_fg = (idx == g_proc_sel) ? C_WHITE : C_TEXT;

        fb_fill(fb, (int32_t)CONTENT_X, (int32_t)ry, CONTENT_W, PROC_ROW_H, row_bg);

        if (idx < nprocs) {
            const ProcEntry& pe = procs[idx];
            fb_text(fb, (int32_t)(CONTENT_X + 4u), (int32_t)(ry + 1u),
                    pe.name, row_fg, row_bg);
            fb_text(fb, (int32_t)(CONTENT_X + PROC_COL1_W), (int32_t)(ry + 1u),
                    pe.is_kernel ? "System" : "Running", row_fg, row_bg);
        }
    }

    uint32_t sb_x = CONTENT_X + CONTENT_W - 12u;
    uint32_t sb_y = PROC_LIST_Y;
    uint32_t sb_h = PROC_VISIBLE * PROC_ROW_H;
    fb_fill(fb, (int32_t)sb_x, (int32_t)sb_y, 12u, sb_h, C_LIST_ALT);
    if (nprocs > (int)PROC_VISIBLE) {
        uint32_t thumb_h = sb_h * PROC_VISIBLE / (uint32_t)nprocs;
        if (thumb_h < 8u) thumb_h = 8u;
        uint32_t thumb_y = sb_y + (uint32_t)(g_proc_scroll * (int32_t)(sb_h - thumb_h) / (nprocs - (int)PROC_VISIBLE));
        fb_fill(fb, (int32_t)sb_x, (int32_t)thumb_y, 12u, thumb_h, C_BTN_REG);

        for (uint32_t i = 0; i < 12u; ++i) {
            fb[thumb_y * SM_W + sb_x + i]              = C_HI;
            fb[(thumb_y + thumb_h - 1u) * SM_W + sb_x + i] = C_SHADOW;
        }
    }

    uint32_t btn_x = CONTENT_X + CONTENT_W - PROC_BTN_W - 4u;
    uint32_t btn_y = SM_CH - PROC_BTN_H - 6u;
    bool can_end = (g_proc_sel > 0);
    fb_button(fb, (int32_t)btn_x, (int32_t)btn_y, PROC_BTN_W, PROC_BTN_H,
              "End Task",
              can_end ? C_BTN_RED : C_BTN_REG,
              can_end ? C_BTN_REDFG : C_SHADOW);
}

static void handle_click(int32_t cx, int32_t cy) {

    for (int t = 0; t < 2; ++t) {
        uint32_t tx = (uint32_t)t * (TAB_W + 2u) + 4u;
        if (cx >= (int32_t)tx && cx < (int32_t)(tx + TAB_W) &&
            cy >= (int32_t)TAB_Y && cy < (int32_t)(TAB_Y + TAB_H)) {
            g_tab = t;
            return;
        }
    }

    if (g_tab == 0) {

        return;
    }

    if (cx >= (int32_t)CONTENT_X && cx < (int32_t)(CONTENT_X + CONTENT_W - 12u) &&
        cy >= (int32_t)PROC_LIST_Y &&
        cy <  (int32_t)(PROC_LIST_Y + PROC_VISIBLE * PROC_ROW_H)) {
        uint32_t rel_y = (uint32_t)(cy - (int32_t)PROC_LIST_Y);
        uint32_t row   = rel_y / PROC_ROW_H;
        if (row < PROC_VISIBLE) {
            int idx = (int)row + g_proc_scroll;
            ProcEntry procs[MAX_PROCS];
            int n = build_proc_list(procs);
            if (idx < n) g_proc_sel = idx;
        }
        return;
    }

    uint32_t btn_x = CONTENT_X + CONTENT_W - PROC_BTN_W - 4u;
    uint32_t btn_y = SM_CH - PROC_BTN_H - 6u;
    if (cx >= (int32_t)btn_x && cx < (int32_t)(btn_x + PROC_BTN_W) &&
        cy >= (int32_t)btn_y && cy < (int32_t)(btn_y + PROC_BTN_H)) {
        if (g_proc_sel > 0) {

            ProcEntry procs[MAX_PROCS];
            int n = build_proc_list(procs);
            if (g_proc_sel < n && procs[g_proc_sel].win) {
                procs[g_proc_sel].win->close_requested = true;
            }
            g_proc_sel = -1;
        }
    }
}

}

namespace sysmon {

void open() {
    if (g_active) return;

    int32_t wx = 100;
    int32_t wy = (int32_t)wm::WM_TITLEBAR_H + 20;
    g_win = wm::win_create(wx, wy, SM_W, SM_H, "System Monitor");
    if (!g_win) return;
    g_active      = true;
    g_tab         = 0;
    g_proc_sel    = -1;
    g_proc_scroll = 0;
    g_frame_counter = 0;
    g_last_sample_t = 0;
    wm::win_mark_dirty(g_win);
}

void close() {
    if (!g_active) return;
    if (g_win) { wm::win_destroy(g_win); g_win = nullptr; }
    g_active = false;
}

bool active() { return g_active; }

void record_frame() {
    if (!g_active) return;
    ++g_frame_counter;
}

void tick(uint64_t ticks_100hz) {
    if (!g_active || !g_win) return;

    if (g_win->close_requested) {
        close();
        return;
    }

    if (g_last_sample_t == 0) g_last_sample_t = ticks_100hz;
    if (ticks_100hz - g_last_sample_t >= 50u) {

        uint32_t val = g_frame_counter * 100u / 20u;
        if (val > 100u) val = 100u;
        g_cpu_hist[g_hist_write] = (uint8_t)val;
        g_hist_write = (g_hist_write + 1u) % HISTORY_LEN;
        g_frame_counter = 0;
        g_last_sample_t = ticks_100hz;
    }

    if (g_win->client_clicked) {
        handle_click(g_win->click_cx, g_win->click_cy);
        g_win->client_clicked = false;
    }

    uint32_t* fb = g_win->client_fb;
    draw_chrome(fb);
    if (g_tab == 0)
        draw_performance(fb, ticks_100hz);
    else
        draw_processes(fb);
    wm::win_mark_dirty(g_win);
}

}
