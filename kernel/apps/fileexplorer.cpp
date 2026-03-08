/*
  fileexplorer.cpp - gui file browser in a wm window
  shows all files and directories in the current vfs path
  single-click selects, double-click opens files in the editor or navigates into dirs
  right-click shows a context menu with open/delete/new options
*/
#include "kernel/apps/fileexplorer.hpp"
#include "kernel/apps/editor.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/ramfs.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/irq/timer.hpp"
#include <stdint.h>
#include <string.h>

static constexpr uint32_t FE_W        = 480u;
static constexpr uint32_t FE_CLIENT_H = 330u;
static constexpr uint32_t FE_H        = FE_CLIENT_H + wm::WIN_TITLEBAR_H;
static constexpr uint32_t FE_FW       = FE_W;

static constexpr uint32_t ADDR_H      = gfx::FONT_H + 6u;
static constexpr uint32_t HDR_H       = gfx::FONT_H + 4u;
static constexpr uint32_t STATUS_H    = gfx::FONT_H + 6u;
static constexpr uint32_t TOOLBAR_H   = gfx::FONT_H + 8u;

static constexpr uint32_t LIST_Y      = TOOLBAR_H + ADDR_H + HDR_H;
static constexpr uint32_t LIST_H      = FE_CLIENT_H - LIST_Y - STATUS_H;
static constexpr uint32_t ROW_H       = gfx::FONT_H + 4u;

static constexpr uint32_t NAME_COL_X  =  4u;
static constexpr uint32_t NAME_COL_W  = 300u;
static constexpr uint32_t SIZE_COL_X  = NAME_COL_X + NAME_COL_W + 4u;
static constexpr uint32_t SIZE_COL_W  = 80u;
static constexpr uint32_t TYPE_COL_X  = SIZE_COL_X + SIZE_COL_W + 4u;

static constexpr uint32_t REFRESH_X   =  6u;
static constexpr uint32_t REFRESH_Y   =  3u;
static constexpr uint32_t REFRESH_W   = 70u;
static constexpr uint32_t REFRESH_H   = TOOLBAR_H - 6u;

static constexpr uint32_t OPEN_X      = REFRESH_X + REFRESH_W + 6u;
static constexpr uint32_t OPEN_W      = 70u;
static constexpr uint32_t OPEN_H      = REFRESH_H;

static constexpr uint32_t VISIBLE_ROWS = LIST_H / ROW_H;

static constexpr uint32_t C_BG        = 0x00C0C0C0u;
static constexpr uint32_t C_LIST_BG   = 0x00FFFFFFu;
static constexpr uint32_t C_LIST_FG   = 0x00000000u;
static constexpr uint32_t C_SEL_BG    = 0x00000080u;
static constexpr uint32_t C_SEL_FG    = 0x00FFFFFFu;
static constexpr uint32_t C_HDR_BG    = 0x00808080u;
static constexpr uint32_t C_HDR_FG    = 0x00FFFFFFu;
static constexpr uint32_t C_ADDR_BG   = 0x00FFFFFFu;
static constexpr uint32_t C_ADDR_FG   = 0x00000000u;
static constexpr uint32_t C_STAT_BG   = 0x00808080u;
static constexpr uint32_t C_STAT_FG   = 0x00FFFFFFu;
static constexpr uint32_t C_DIR_FG    = 0x00800080u;
static constexpr uint32_t C_BTN       = 0x00C0C0C0u;
static constexpr uint32_t C_BTN_FG    = 0x00000000u;
static constexpr uint32_t C_HI        = 0x00FFFFFFu;
static constexpr uint32_t C_SHADOW    = 0x00404040u;
static constexpr uint32_t C_STRIP     = 0x00F0F0F0u;

static constexpr uint32_t MAX_ENTRIES = 128u;

namespace {

struct FileEntry {
    char     name[64];
    uint32_t size;
    bool     is_dir;
    bool     is_dotdot;
};

static char g_cur_path[128] = "";

static FileEntry g_entries[MAX_ENTRIES];
static uint32_t  g_nentries = 0;
static uint32_t  g_scroll   = 0;
static int32_t   g_selected = -1;

static constexpr uint64_t DBLCLICK_TICKS = 50u;
static int32_t  g_last_click_row  = -1;
static uint64_t g_last_click_tick = 0u;

static wm::Window* g_win   = nullptr;
static bool        g_dirty = true;

static uint32_t g_new_file_ctr = 0;
static uint32_t g_new_dir_ctr  = 0;

static constexpr uint32_t CTX_MAX_ITEMS = 5u;
static constexpr uint32_t CTX_ITEM_H   = gfx::FONT_H + 6u;
static constexpr uint32_t CTX_W        = 148u;
static constexpr uint32_t CTX_PADDING  = 3u;

enum CtxAction {
    CTX_NONE = 0, CTX_OPEN, CTX_DELETE,
    CTX_NEW_FILE, CTX_NEW_DIR, CTX_REFRESH
};

static bool        g_ctx_open    = false;
static int32_t     g_ctx_row     = -1;
static uint32_t    g_ctx_x       = 0;
static uint32_t    g_ctx_y       = 0;
static CtxAction   g_ctx_actions[CTX_MAX_ITEMS];
static const char* g_ctx_labels [CTX_MAX_ITEMS];
static uint32_t    g_ctx_nitems  = 0;

static void make_full_path(char* out, uint32_t out_size,
                            const char* dir, const char* name) {
    uint32_t di = 0, ni = 0, oi = 0;
    if (dir && dir[0]) {
        while (dir[di] && oi + 1u < out_size) out[oi++] = dir[di++];
        if (oi + 1u < out_size) out[oi++] = '/';
    }
    while (name[ni] && oi + 1u < out_size) out[oi++] = name[ni++];
    out[oi] = '\0';
}

static void ls_cb(const char* name, size_t size, bool is_dir) {
    if (g_nentries >= MAX_ENTRIES) return;
    FileEntry& e = g_entries[g_nentries++];
    uint32_t i = 0;
    while (name[i] && i < 63u) { e.name[i] = name[i]; ++i; }
    e.name[i]   = '\0';
    e.size      = (uint32_t)size;
    e.is_dir    = is_dir;
    e.is_dotdot = false;
}

static void scan_dir() {
    g_nentries = 0;
    g_scroll   = 0;
    g_selected = -1;
    g_ctx_open = false;

    if (g_cur_path[0] != '\0') {
        FileEntry& dd = g_entries[g_nentries++];
        dd.name[0] = '.'; dd.name[1] = '.'; dd.name[2] = '\0';
        dd.size      = 0;
        dd.is_dir    = true;
        dd.is_dotdot = true;
    }
    vfs::ls_in(g_cur_path, ls_cb);
    g_dirty = true;
}

static void navigate_into(const char* child_name) {
    uint32_t plen = 0;
    while (g_cur_path[plen]) ++plen;
    uint32_t nlen = 0;
    while (child_name[nlen]) ++nlen;
    if (plen == 0) {
        uint32_t i = 0;
        while (child_name[i] && i < 127u) { g_cur_path[i] = child_name[i]; ++i; }
        g_cur_path[i] = '\0';
    } else {
        if (plen + 1u + nlen < 127u) {
            g_cur_path[plen] = '/';
            uint32_t i = 0;
            while (child_name[i]) { g_cur_path[plen + 1u + i] = child_name[i]; ++i; }
            g_cur_path[plen + 1u + i] = '\0';
        }
    }
    scan_dir();
}

static void navigate_up() {
    uint32_t plen = 0;
    while (g_cur_path[plen]) ++plen;
    if (plen == 0) return;
    int32_t slash = -1;
    for (int32_t i = (int32_t)plen - 1; i >= 0; --i) {
        if (g_cur_path[i] == '/') { slash = i; break; }
    }
    if (slash < 0) {
        g_cur_path[0] = '\0';
    } else {
        g_cur_path[slash] = '\0';
    }
    scan_dir();
}

static void fb_fill(uint32_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    for (uint32_t dy = 0; dy < h && (y+dy) < FE_CLIENT_H; ++dy)
        for (uint32_t dx = 0; dx < w && (x+dx) < FE_W; ++dx)
            fb[(y+dy)*FE_FW + (x+dx)] = c;
}

static void fb_text(uint32_t* fb, uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg) {
    for (; *s && x + gfx::FONT_W <= FE_W; ++s, x += gfx::FONT_W)
        gfx::draw_char_into(fb, FE_FW, x, y, *s, fg, bg);
}

static void fb_text_clip(uint32_t* fb, uint32_t x, uint32_t y,
                          const char* s, uint32_t fg, uint32_t bg,
                          uint32_t max_px) {
    uint32_t drawn = 0;
    for (; *s && drawn + gfx::FONT_W <= max_px && x + gfx::FONT_W <= FE_W;
         ++s, x += gfx::FONT_W, drawn += gfx::FONT_W)
        gfx::draw_char_into(fb, FE_FW, x, y, *s, fg, bg);
}

static void fb_uint_right(uint32_t* fb, uint32_t field_x, uint32_t y,
                           uint32_t field_w, uint32_t val,
                           uint32_t fg, uint32_t bg) {
    char tmp[12]; int n = 0;
    if (val == 0) { tmp[n++] = '0'; }
    else { uint32_t v = val; while (v) { tmp[n++]=(char)('0'+v%10); v/=10; } }
    for (int a=0,b=n-1; a<b; ++a,--b) { char t=tmp[a]; tmp[a]=tmp[b]; tmp[b]=t; }
    tmp[n] = '\0';
    uint32_t text_px = (uint32_t)n * gfx::FONT_W;
    uint32_t x = (text_px < field_w) ? (field_x + field_w - text_px) : field_x;
    fb_text(fb, x, y, tmp, fg, bg);
}

static void fb_button(uint32_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const char* label, bool pressed) {
    uint32_t fill = pressed ? 0x00A0A0A0u : C_BTN;
    fb_fill(fb, x, y, w, h, fill);
    for (uint32_t i = 0; i < w; ++i) {
        if (y < FE_CLIENT_H)      fb[y*FE_FW+x+i]       = pressed ? C_SHADOW : C_HI;
        if (y+h-1 < FE_CLIENT_H) fb[(y+h-1)*FE_FW+x+i] = pressed ? C_HI : C_SHADOW;
    }
    for (uint32_t i = 1; i+1 < h; ++i) {
        if (y+i < FE_CLIENT_H) {
            fb[(y+i)*FE_FW+x]     = pressed ? C_SHADOW : C_HI;
            fb[(y+i)*FE_FW+x+w-1] = pressed ? C_HI : C_SHADOW;
        }
    }
    uint32_t llen = 0; while (label[llen]) ++llen;
    uint32_t lx = x + (w > llen*gfx::FONT_W ? (w - llen*gfx::FONT_W)/2u : 0u);
    uint32_t ly = y + (h > gfx::FONT_H       ? (h - gfx::FONT_H)/2u : 0u);
    fb_text(fb, lx, ly, label, C_BTN_FG, fill);
}

static void redraw() {
    if (!g_win || !g_win->client_fb) return;
    uint32_t* fb = g_win->client_fb;

    fb_fill(fb, 0, 0, FE_W, FE_CLIENT_H, C_BG);

    fb_fill(fb, 0, 0, FE_W, TOOLBAR_H, C_BG);
    fb_button(fb, REFRESH_X, REFRESH_Y, REFRESH_W, REFRESH_H, "Refresh", false);
    fb_button(fb, OPEN_X,    REFRESH_Y, OPEN_W,    OPEN_H,    "Open",    false);

    uint32_t addr_y = TOOLBAR_H;
    fb_fill(fb, 0, addr_y, FE_W, ADDR_H, C_BG);

    fb_fill(fb, 80u, addr_y+2u, FE_W - 84u, ADDR_H - 4u, C_ADDR_BG);

    fb_text(fb, 4u, addr_y + (ADDR_H - gfx::FONT_H)/2u, "Path:", C_LIST_FG, C_BG);

    {
        char path_disp[64];
        uint32_t pi = 0;
        path_disp[pi++] = '/';
        const char* cp = g_cur_path;
        for (; *cp && pi < 62u; ++cp) path_disp[pi++] = *cp;
        path_disp[pi] = '\0';
        fb_text_clip(fb, 84u, addr_y + (ADDR_H - gfx::FONT_H)/2u,
                     path_disp, C_ADDR_FG, C_ADDR_BG, FE_W - 88u);
    }

    uint32_t hdr_y = TOOLBAR_H + ADDR_H;
    fb_fill(fb, 0, hdr_y, FE_W, HDR_H, C_HDR_BG);
    fb_text(fb, NAME_COL_X + 2u, hdr_y + 2u, "Name",       C_HDR_FG, C_HDR_BG);
    fb_text(fb, SIZE_COL_X + 2u, hdr_y + 2u, "Size (B)",   C_HDR_FG, C_HDR_BG);
    fb_text(fb, TYPE_COL_X + 2u, hdr_y + 2u, "Type",       C_HDR_FG, C_HDR_BG);

    for (uint32_t dy = 0; dy < HDR_H; ++dy) {
        if (hdr_y+dy < FE_CLIENT_H) {
            fb[(hdr_y+dy)*FE_FW + SIZE_COL_X - 2u] = C_SHADOW;
            fb[(hdr_y+dy)*FE_FW + TYPE_COL_X - 2u] = C_SHADOW;
        }
    }

    fb_fill(fb, 0, LIST_Y, FE_W, LIST_H, C_LIST_BG);
    uint32_t ry = LIST_Y;
    for (uint32_t i = 0; i < VISIBLE_ROWS && (g_scroll + i) < g_nentries; ++i) {
        uint32_t ei = g_scroll + i;
        const FileEntry& e = g_entries[ei];
        bool sel  = ((int32_t)ei == g_selected);
        bool odd  = (ei & 1u) != 0u;
        uint32_t row_bg = sel  ? C_SEL_BG :
                          odd  ? C_STRIP  : C_LIST_BG;
        uint32_t row_fg = sel  ? C_SEL_FG : C_LIST_FG;

        fb_fill(fb, 0, ry, FE_W, ROW_H, row_bg);

        uint32_t text_y = ry + (ROW_H - gfx::FONT_H) / 2u;

        if (e.is_dir) {
            fb_text(fb, NAME_COL_X + 2u, text_y, "[D] ", e.is_dir ? C_DIR_FG : row_fg, row_bg);
            fb_text_clip(fb, NAME_COL_X + 2u + 4u*gfx::FONT_W, text_y,
                         e.name, e.is_dir ? C_DIR_FG : row_fg, row_bg,
                         NAME_COL_W - 4u*gfx::FONT_W - 4u);
        } else {
            fb_text_clip(fb, NAME_COL_X + 2u, text_y,
                         e.name, row_fg, row_bg, NAME_COL_W - 4u);
        }

        if (!e.is_dir)
            fb_uint_right(fb, SIZE_COL_X, text_y, SIZE_COL_W - 4u,
                          e.size, row_fg, row_bg);
        else
            fb_text(fb, SIZE_COL_X + 2u, text_y, "--", row_fg, row_bg);

        const char* tstr = e.is_dir ? "Folder" : "File";
        fb_text(fb, TYPE_COL_X + 2u, text_y, tstr, row_fg, row_bg);

        if (ry + ROW_H - 1u < FE_CLIENT_H - STATUS_H)
            for (uint32_t dx = 0; dx < FE_W; ++dx)
                fb[(ry + ROW_H - 1u)*FE_FW + dx] = sel ? C_SEL_BG : 0x00D8D8D8u;

        ry += ROW_H;
    }

    if (ry < LIST_Y + LIST_H)
        fb_fill(fb, 0, ry, FE_W, LIST_Y + LIST_H - ry, C_LIST_BG);

    if (g_nentries > VISIBLE_ROWS) {
        uint32_t bar_x   = FE_W - 6u;
        uint32_t bar_h   = LIST_H * VISIBLE_ROWS / g_nentries;
        uint32_t bar_y   = LIST_Y + (uint32_t)(LIST_H * g_scroll / g_nentries);
        fb_fill(fb, bar_x, LIST_Y, 6u, LIST_H, 0x00D0D0D0u);
        fb_fill(fb, bar_x, bar_y,  6u, (bar_h > 4u ? bar_h : 4u), 0x00808080u);
    }

    uint32_t stat_y = FE_CLIENT_H - STATUS_H;
    fb_fill(fb, 0, stat_y, FE_W, STATUS_H, C_STAT_BG);
    fb_fill(fb, 0, stat_y, FE_W, 1u, C_SHADOW);

    char stat_buf[80];
    uint32_t si = 0;

    {
        uint32_t v = g_nentries; char tmp[8]; int n=0;
        if (!v) tmp[n++]='0'; else while(v){tmp[n++]=(char)('0'+v%10);v/=10;}
        for (int a=0,b=n-1;a<b;++a,--b){char t=tmp[a];tmp[a]=tmp[b];tmp[b]=t;}
        tmp[n]='\0';
        for (int i=0;tmp[i];++i) if(si<79) stat_buf[si++]=tmp[i];
    }
    const char* suffix = " items";
    for (;*suffix && si<79;++suffix) stat_buf[si++]=*suffix;
    if (g_selected >= 0 && (uint32_t)g_selected < g_nentries) {
        const char* sep = "   |   ";
        for (;*sep && si<79;++sep) stat_buf[si++]=*sep;
        const char* nm = g_entries[g_selected].name;
        for (;*nm && si<79;++nm) stat_buf[si++]=*nm;
    }
    stat_buf[si] = '\0';
    fb_text(fb, 8u, stat_y + (STATUS_H - gfx::FONT_H)/2u, stat_buf, C_STAT_FG, C_STAT_BG);

    if (g_ctx_open && g_ctx_nitems > 0) {
        uint32_t mh = g_ctx_nitems * CTX_ITEM_H + 2u * CTX_PADDING;
        uint32_t mx = g_ctx_x;
        uint32_t my = g_ctx_y;

        if (mx + CTX_W > FE_W) mx = (FE_W > CTX_W) ? FE_W - CTX_W : 0u;
        if (my + mh > FE_CLIENT_H) my = (g_ctx_y > mh) ? g_ctx_y - mh : 0u;

        fb_fill(fb, mx, my, CTX_W, mh, 0x00C0C0C0u);

        for (uint32_t i = 0; i < CTX_W && mx+i < FE_W; ++i) {
            if (my < FE_CLIENT_H)      fb[my*FE_FW + mx+i]       = 0x00FFFFFFu;
            if (my+mh-1u < FE_CLIENT_H) fb[(my+mh-1u)*FE_FW + mx+i] = 0x00404040u;
        }
        for (uint32_t i = 0; i < mh && my+i < FE_CLIENT_H; ++i) {
            fb[(my+i)*FE_FW + mx]              = 0x00FFFFFFu;
            if (mx+CTX_W-1u < FE_W)
                fb[(my+i)*FE_FW + mx+CTX_W-1u] = 0x00404040u;
        }

        for (uint32_t k = 0; k < g_ctx_nitems; ++k) {
            uint32_t iy     = my + CTX_PADDING + k * CTX_ITEM_H;
            uint32_t text_y = iy + (CTX_ITEM_H - gfx::FONT_H) / 2u;
            fb_fill(fb, mx+1u, iy, CTX_W-2u, CTX_ITEM_H, 0x00C0C0C0u);
            fb_text_clip(fb, mx + 8u, text_y,
                         g_ctx_labels[k], 0x00000000u, 0x00C0C0C0u,
                         CTX_W - 12u);

            if (k + 1u < g_ctx_nitems && iy + CTX_ITEM_H < FE_CLIENT_H)
                for (uint32_t di = 2u; di < CTX_W - 2u && mx+di < FE_W; ++di)
                    fb[(iy + CTX_ITEM_H - 1u)*FE_FW + mx+di] = 0x00A0A0A0u;
        }
    }

    wm::win_mark_dirty(g_win);
}

static void open_entry(int32_t idx) {
    if (idx < 0 || (uint32_t)idx >= g_nentries) return;
    const FileEntry& e = g_entries[idx];
    if (e.is_dotdot) { navigate_up(); return; }
    if (e.is_dir)    { navigate_into(e.name); return; }

    char full[128];
    make_full_path(full, sizeof(full), g_cur_path, e.name);
    if (!editor::active())
        editor::open(full);
}

static void open_context_menu(int32_t row_idx, uint32_t cx, uint32_t cy) {
    g_ctx_row    = row_idx;
    g_ctx_nitems = 0;
    g_ctx_x      = cx;
    g_ctx_y      = cy;
    if (row_idx >= 0 && (uint32_t)row_idx < g_nentries) {
        const FileEntry& e = g_entries[row_idx];
        if (e.is_dotdot) {
            g_ctx_labels [g_ctx_nitems] = "Go Up";
            g_ctx_actions[g_ctx_nitems] = CTX_OPEN;
            ++g_ctx_nitems;
        } else if (e.is_dir) {
            g_ctx_labels [g_ctx_nitems] = "Open Folder";
            g_ctx_actions[g_ctx_nitems] = CTX_OPEN;
            ++g_ctx_nitems;
            g_ctx_labels [g_ctx_nitems] = "Delete Folder";
            g_ctx_actions[g_ctx_nitems] = CTX_DELETE;
            ++g_ctx_nitems;
        } else {
            g_ctx_labels [g_ctx_nitems] = "Open in Editor";
            g_ctx_actions[g_ctx_nitems] = CTX_OPEN;
            ++g_ctx_nitems;
            g_ctx_labels [g_ctx_nitems] = "Delete File";
            g_ctx_actions[g_ctx_nitems] = CTX_DELETE;
            ++g_ctx_nitems;
        }
    } else {

        g_ctx_labels [g_ctx_nitems] = "New File";
        g_ctx_actions[g_ctx_nitems] = CTX_NEW_FILE;
        ++g_ctx_nitems;
        g_ctx_labels [g_ctx_nitems] = "New Folder";
        g_ctx_actions[g_ctx_nitems] = CTX_NEW_DIR;
        ++g_ctx_nitems;
        g_ctx_labels [g_ctx_nitems] = "Refresh";
        g_ctx_actions[g_ctx_nitems] = CTX_REFRESH;
        ++g_ctx_nitems;
    }
    g_ctx_open = true;
    g_dirty    = true;
}

static void exec_ctx_action(uint32_t item_idx) {
    if (item_idx >= g_ctx_nitems) return;
    CtxAction act = g_ctx_actions[item_idx];
    int32_t   row = g_ctx_row;
    g_ctx_open = false;
    switch (act) {
    case CTX_OPEN:
        open_entry(row);
        break;
    case CTX_DELETE: {
        if (row < 0 || (uint32_t)row >= g_nentries) break;
        char full[128];
        make_full_path(full, sizeof(full), g_cur_path, g_entries[row].name);
        ramfs::remove(full);
        scan_dir();
        break;
    }
    case CTX_NEW_FILE: {

        ++g_new_file_ctr;
        char idx_buf[8]; uint32_t n = 0;
        uint32_t v = g_new_file_ctr;
        if (!v) { idx_buf[n++] = '0'; }
        else { while (v) { idx_buf[n++] = (char)('0' + v % 10); v /= 10; } }
        for (uint32_t a=0,b=n-1;a<b;++a,--b){char t=idx_buf[a];idx_buf[a]=idx_buf[b];idx_buf[b]=t;}
        idx_buf[n] = '\0';
        char name[32]; uint32_t ni = 0;
        const char* pfx = "newfile"; while (*pfx) name[ni++] = *pfx++;
        uint32_t ii = 0; while (idx_buf[ii]) name[ni++] = idx_buf[ii++];
        const char* sfx = ".txt"; while (*sfx) name[ni++] = *sfx++;
        name[ni] = '\0';
        char full[128];
        make_full_path(full, sizeof(full), g_cur_path, name);
        vfs::write(full, nullptr, 0);
        scan_dir();
        break;
    }
    case CTX_NEW_DIR: {
        ++g_new_dir_ctr;
        char idx_buf[8]; uint32_t n = 0;
        uint32_t v = g_new_dir_ctr;
        if (!v) { idx_buf[n++] = '0'; }
        else { while (v) { idx_buf[n++] = (char)('0' + v % 10); v /= 10; } }
        for (uint32_t a=0,b=n-1;a<b;++a,--b){char t=idx_buf[a];idx_buf[a]=idx_buf[b];idx_buf[b]=t;}
        idx_buf[n] = '\0';
        char name[32]; uint32_t ni = 0;
        const char* pfx = "folder"; while (*pfx) name[ni++] = *pfx++;
        uint32_t ii = 0; while (idx_buf[ii]) name[ni++] = idx_buf[ii++];
        name[ni] = '\0';
        char full[128];
        make_full_path(full, sizeof(full), g_cur_path, name);
        vfs::mkdir(full);
        scan_dir();
        break;
    }
    case CTX_REFRESH:
        scan_dir();
        break;
    default: break;
    }
    g_dirty = true;
}

}

namespace fileexplorer {

void open() {
    if (g_win) return;
    g_win = wm::win_create(100, 80, FE_W, FE_H, "File Explorer");
    if (!g_win) return;
    g_cur_path[0] = '\0';
    scan_dir();
    redraw();
}

void close() {
    if (!g_win) return;
    wm::win_destroy(g_win);
    g_win = nullptr;
}

bool active() { return g_win != nullptr; }

void tick(uint64_t ticks) {
    if (!g_win) return;

    if (g_win->close_requested) { close(); return; }

    if (g_win->right_clicked) {
        g_win->right_clicked = false;
        int32_t rcx = g_win->right_cx;
        int32_t rcy = g_win->right_cy;
        int32_t target_row = -1;
        if (rcy >= (int32_t)LIST_Y && rcy < (int32_t)(LIST_Y + LIST_H)) {
            int32_t ri = (rcy - (int32_t)LIST_Y) / (int32_t)ROW_H;
            uint32_t ei = g_scroll + (uint32_t)ri;
            if (ei < g_nentries) target_row = (int32_t)ei;
        }
        open_context_menu(target_row, (uint32_t)rcx, (uint32_t)rcy);
        g_dirty = true;
    }

    if (g_win->client_clicked) {
        g_win->client_clicked = false;
        int32_t cx = g_win->click_cx;
        int32_t cy = g_win->click_cy;

        if (g_ctx_open) {
            uint32_t mh = g_ctx_nitems * CTX_ITEM_H + 2u * CTX_PADDING;
            uint32_t mx = g_ctx_x;
            uint32_t my = g_ctx_y;
            if (mx + CTX_W > FE_W) mx = (FE_W > CTX_W) ? FE_W - CTX_W : 0u;
            if (my + mh > FE_CLIENT_H) my = (g_ctx_y > mh) ? g_ctx_y - mh : 0u;
            if (cx >= (int32_t)mx && cx < (int32_t)(mx + CTX_W) &&
                cy >= (int32_t)my && cy < (int32_t)(my + mh)) {
                int32_t rel = cy - (int32_t)(my + CTX_PADDING);
                if (rel >= 0) exec_ctx_action((uint32_t)rel / CTX_ITEM_H);
            } else {
                g_ctx_open = false;
            }
            g_dirty = true;
            return;
        }

        if (cy >= (int32_t)REFRESH_Y && cy < (int32_t)(REFRESH_Y + REFRESH_H)) {
            if (cx >= (int32_t)REFRESH_X && cx < (int32_t)(REFRESH_X + REFRESH_W)) {
                scan_dir(); g_dirty = true; return;
            }
            if (cx >= (int32_t)OPEN_X && cx < (int32_t)(OPEN_X + OPEN_W)) {
                open_entry(g_selected); return;
            }
        }

        if (cy >= (int32_t)LIST_Y && cy < (int32_t)(LIST_Y + LIST_H)) {
            int32_t row_idx = (cy - (int32_t)LIST_Y) / (int32_t)ROW_H;
            uint32_t ei = g_scroll + (uint32_t)row_idx;
            if (ei < g_nentries) {

                bool dbl = ((int32_t)ei == g_last_click_row) &&
                           (ticks - g_last_click_tick <= DBLCLICK_TICKS);
                g_last_click_row  = (int32_t)ei;
                g_last_click_tick = ticks;

                if (dbl) {
                    g_selected = (int32_t)ei;
                    open_entry((int32_t)ei);
                } else {
                    g_selected = (int32_t)ei;
                }
                g_dirty = true;
            }
        }
    }

    if (g_dirty) {
        g_dirty = false;
        redraw();
    }
}

}
