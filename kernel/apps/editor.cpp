/*
  editor.cpp - line-based text editor in a wm window
  supports arrow keys, home/end, pgup/pgdn, backspace, delete, enter, tab
  ctrl+s saves, ctrl+q quits without saving, ctrl+x saves and quits
  buffers up to 512 lines of 255 chars each, backed by the vfs
*/
#include "kernel/apps/editor.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/core/print.hpp"
#include "kernel/shell/shell.hpp"
#include <string.h>
#include <stdint.h>

static constexpr uint32_t ED_W          = 640;

static constexpr uint32_t ED_CLIENT_H   = 388u;
static constexpr uint32_t ED_H          = ED_CLIENT_H + wm::WIN_TITLEBAR_H;
static constexpr uint32_t ED_HEADER_H   = gfx::FONT_H;
static constexpr uint32_t ED_FOOTER_H   = gfx::FONT_H;
static constexpr uint32_t ED_TEXT_Y     = ED_HEADER_H;
static constexpr uint32_t ED_TEXT_H     = ED_CLIENT_H - ED_HEADER_H - ED_FOOTER_H;
static constexpr uint32_t ED_TEXT_ROWS  = ED_TEXT_H  / gfx::FONT_H;
static constexpr uint32_t ED_TEXT_COLS  = ED_W       / gfx::FONT_W;

static constexpr uint32_t MAX_LINES    = 512u;
static constexpr uint32_t MAX_LINE_LEN = 255u;

namespace {

static char     g_lines   [MAX_LINES][MAX_LINE_LEN + 1];
static uint16_t g_line_len[MAX_LINES];
static uint32_t g_n_lines = 0;

static uint32_t g_cur_row  = 0;
static uint32_t g_cur_col  = 0;
static uint32_t g_top_line = 0;
static uint32_t g_left_col = 0;

static wm::Window* g_win      = nullptr;
static bool        g_active   = false;
static bool        g_modified = false;
static char        g_path[64] = {};
static bool        g_dirty    = true;

static uint64_t g_blink_last = 0;
static bool     g_blink_on   = true;
static constexpr uint64_t BLINK_PERIOD = 50u;

static constexpr uint32_t C_BG        = 0x00141414u;
static constexpr uint32_t C_FG        = 0x00d4d4d4u;
static constexpr uint32_t C_HEADER_BG = 0x00253535u;
static constexpr uint32_t C_HEADER_FG = 0x00e0e0e0u;
static constexpr uint32_t C_FOOTER_BG = 0x00253535u;
static constexpr uint32_t C_FOOTER_FG = 0x00b0b0b0u;
static constexpr uint32_t C_CUR_BG    = 0x00b6599bu;
static constexpr uint32_t C_CUR_FG    = 0x00ffffffu;
static constexpr uint32_t C_ACCENT    = 0x00b6599bu;
static constexpr uint32_t C_HINT      = 0x00606060u;
static constexpr uint32_t C_MODIF     = 0x00e07040u;
static constexpr uint32_t C_PROMPT_BG = 0x00003030u;
static constexpr uint32_t C_PROMPT_FG = 0x00ffffc0u;
static constexpr uint32_t C_PROMPT_IN = 0x00ffffffu;

static bool  g_naming       = false;
static char  g_name_buf[64] = {};
static uint32_t g_name_len  = 0;
static bool  g_name_then_quit = false;

static void fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t col) {
    uint32_t* fb = g_win->client_fb;
    uint32_t  fw = g_win->w;
    uint32_t  fh = g_win->client_h;
    for (uint32_t py = y; py < y + h && py < fh; ++py)
        for (uint32_t px = x; px < x + w && px < fw; ++px)
            fb[py * fw + px] = col;
}

static void fb_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (x + gfx::FONT_W > ED_W || y + gfx::FONT_H > ED_CLIENT_H) return;
    gfx::draw_char_into(g_win->client_fb, g_win->w, x, y, c, fg, bg);
}

static uint32_t fb_text(uint32_t x, uint32_t y, const char* s,
                         uint32_t fg, uint32_t bg) {
    while (*s && x + gfx::FONT_W <= ED_W) {
        fb_char(x, y, *s++, fg, bg);
        x += gfx::FONT_W;
    }
    return x;
}

static uint32_t fb_uint(uint32_t x, uint32_t y, uint32_t v,
                         uint32_t fg, uint32_t bg) {
    char tmp[12]; int ti = 0;
    do { tmp[ti++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = ti - 1; i >= 0; --i) {
        if (x + gfx::FONT_W > ED_W) break;
        fb_char(x, y, tmp[i], fg, bg);
        x += gfx::FONT_W;
    }
    return x;
}

static void render_header() {
    if (g_naming) {

        fb_fill(0, 0, ED_W, ED_HEADER_H, C_PROMPT_BG);
        uint32_t x = fb_text(4, 0, "Save as: ", C_PROMPT_FG, C_PROMPT_BG);
        x = fb_text(x, 0, g_name_buf, C_PROMPT_IN, C_PROMPT_BG);

        if (g_blink_on)
            fb_char(x, 0, '_', C_PROMPT_FG, C_PROMPT_BG);
        static const char* chint = "Enter=save  ^C=cancel";
        uint32_t hl = 0; while (chint[hl]) ++hl;
        fb_text(ED_W - hl * gfx::FONT_W - 4, 0, chint, C_HINT, C_PROMPT_BG);
        return;
    }

    fb_fill(0, 0, ED_W, ED_HEADER_H, C_HEADER_BG);

    const char* name = g_path[0] ? g_path : "[untitled]";
    uint32_t x = fb_text(4, 0, name, C_HEADER_FG, C_HEADER_BG);
    if (g_modified)
        fb_text(x + 8, 0, "[+]", C_MODIF, C_HEADER_BG);

    static const char* hint = "^S save  ^Q quit  ^X save+quit";
    uint32_t hl = 0; while (hint[hl]) ++hl;
    fb_text(ED_W - hl * gfx::FONT_W - 4, 0, hint, C_HINT, C_HEADER_BG);
}

static void render_footer() {
    uint32_t fy = ED_CLIENT_H - ED_FOOTER_H;
    fb_fill(0, fy, ED_W, ED_FOOTER_H, C_FOOTER_BG);

    uint32_t x = 4;
    x = fb_text(x, fy, "Ln ", C_FOOTER_FG, C_FOOTER_BG);
    x = fb_uint(x, fy, g_cur_row + 1, C_ACCENT, C_FOOTER_BG);
    x = fb_text(x, fy, "  Col ", C_FOOTER_FG, C_FOOTER_BG);
    x = fb_uint(x, fy, g_cur_col + 1, C_ACCENT, C_FOOTER_BG);
    x = fb_text(x, fy, "  of ", C_FOOTER_FG, C_FOOTER_BG);
    fb_uint(x, fy, g_n_lines, C_FOOTER_FG, C_FOOTER_BG);
}

static void render_text_area() {
    fb_fill(0, ED_TEXT_Y, ED_W, ED_TEXT_H, C_BG);

    for (uint32_t vrow = 0; vrow < ED_TEXT_ROWS; ++vrow) {
        uint32_t line_idx = g_top_line + vrow;
        if (line_idx >= g_n_lines) break;

        uint32_t py       = ED_TEXT_Y + vrow * gfx::FONT_H;
        uint32_t line_len = g_line_len[line_idx];

        for (uint32_t vcol = 0; vcol < ED_TEXT_COLS; ++vcol) {
            uint32_t char_idx = g_left_col + vcol;
            uint32_t px       = vcol * gfx::FONT_W;

            bool is_cursor = (line_idx == g_cur_row)
                          && (char_idx == g_cur_col)
                          && g_blink_on;

            char c = (char_idx < line_len) ? g_lines[line_idx][char_idx] : ' ';
            if ((uint8_t)c < 0x20u) c = ' ';

            uint32_t fg = is_cursor ? C_CUR_FG : C_FG;
            uint32_t bg = is_cursor ? C_CUR_BG : C_BG;
            fb_char(px, py, c, fg, bg);
        }
    }
}

static void do_render() {
    if (!g_win) return;
    render_header();
    render_text_area();
    render_footer();
    wm::win_mark_dirty(g_win);
    g_dirty = false;
}

static char io_buf[MAX_LINES * (MAX_LINE_LEN + 2)];

static void load_file() {
    memset(g_lines, 0, sizeof(g_lines));
    memset(g_line_len, 0, sizeof(g_line_len));
    g_n_lines = 0;

    if (!g_path[0]) { g_n_lines = 1; return; }

    int32_t len = vfs::read(g_path, io_buf, sizeof(io_buf) - 1);
    if (len <= 0) { g_n_lines = 1; return; }
    io_buf[len] = '\0';

    uint32_t row = 0, col = 0;
    for (int32_t i = 0; i <= len && row < MAX_LINES; ++i) {
        char c = io_buf[i];
        if (c == '\n' || c == '\0') {
            g_lines[row][col] = '\0';
            g_line_len[row]   = (uint16_t)col;
            ++row; col = 0;
            if (c == '\0') break;
        } else if (c != '\r' && col < MAX_LINE_LEN) {
            g_lines[row][col++] = c;
        }
    }
    g_n_lines = (row == 0) ? 1u : row;
}

static void save_file() {
    if (!g_path[0]) return;

    uint32_t pos = 0;
    for (uint32_t i = 0; i < g_n_lines && pos + MAX_LINE_LEN + 2 < sizeof(io_buf); ++i) {
        uint32_t ll = g_line_len[i];
        for (uint32_t j = 0; j < ll; ++j)
            io_buf[pos++] = g_lines[i][j];
        io_buf[pos++] = '\n';
    }
    vfs::write(g_path, io_buf, pos);
    g_modified = false;
    g_dirty    = true;
}

static void clamp_col() {
    uint32_t len = g_line_len[g_cur_row];
    if (g_cur_col > len) g_cur_col = len;
}

static void ensure_viewport() {
    if (g_cur_row < g_top_line)
        g_top_line = g_cur_row;
    if (g_cur_row >= g_top_line + ED_TEXT_ROWS)
        g_top_line = g_cur_row - ED_TEXT_ROWS + 1;

    if (g_cur_col < g_left_col)
        g_left_col = g_cur_col;
    if (g_cur_col >= g_left_col + ED_TEXT_COLS)
        g_left_col = g_cur_col - ED_TEXT_COLS + 1;
}

static void insert_char(char c) {
    uint32_t row = g_cur_row;
    uint32_t col = g_cur_col;
    uint32_t len = g_line_len[row];
    if (len >= MAX_LINE_LEN) return;
    for (uint32_t i = len; i > col; --i)
        g_lines[row][i] = g_lines[row][i - 1];
    g_lines[row][col] = c;
    g_line_len[row]   = (uint16_t)(len + 1);
    g_lines[row][g_line_len[row]] = '\0';
    g_cur_col++;
    g_modified = true;
}

static void delete_before() {
    if (g_cur_col > 0) {
        uint32_t row = g_cur_row;
        uint32_t col = g_cur_col;
        uint32_t len = g_line_len[row];
        for (uint32_t i = col - 1; i < len - 1; ++i)
            g_lines[row][i] = g_lines[row][i + 1];
        g_line_len[row] = (uint16_t)(len - 1);
        g_lines[row][g_line_len[row]] = '\0';
        g_cur_col--;
        g_modified = true;
    } else if (g_cur_row > 0) {

        uint32_t row      = g_cur_row;
        uint32_t prev     = row - 1;
        uint32_t prev_len = g_line_len[prev];
        uint32_t this_len = g_line_len[row];
        if (prev_len + this_len <= MAX_LINE_LEN) {
            for (uint32_t i = 0; i < this_len; ++i)
                g_lines[prev][prev_len + i] = g_lines[row][i];
            g_line_len[prev] = (uint16_t)(prev_len + this_len);
            g_lines[prev][g_line_len[prev]] = '\0';
            for (uint32_t i = row; i < g_n_lines - 1; ++i) {
                memcpy(g_lines[i], g_lines[i + 1], MAX_LINE_LEN + 1);
                g_line_len[i] = g_line_len[i + 1];
            }
            g_n_lines--;
            g_cur_row = prev;
            g_cur_col = prev_len;
            g_modified = true;
        }
    }
}

static void delete_at() {
    uint32_t row = g_cur_row;
    uint32_t col = g_cur_col;
    uint32_t len = g_line_len[row];
    if (col < len) {
        for (uint32_t i = col; i < len - 1; ++i)
            g_lines[row][i] = g_lines[row][i + 1];
        g_line_len[row] = (uint16_t)(len - 1);
        g_lines[row][g_line_len[row]] = '\0';
        g_modified = true;
    } else if (row + 1 < g_n_lines) {

        uint32_t next     = row + 1;
        uint32_t next_len = g_line_len[next];
        if (len + next_len <= MAX_LINE_LEN) {
            for (uint32_t i = 0; i < next_len; ++i)
                g_lines[row][len + i] = g_lines[next][i];
            g_line_len[row] = (uint16_t)(len + next_len);
            g_lines[row][g_line_len[row]] = '\0';
            for (uint32_t i = next; i < g_n_lines - 1; ++i) {
                memcpy(g_lines[i], g_lines[i + 1], MAX_LINE_LEN + 1);
                g_line_len[i] = g_line_len[i + 1];
            }
            g_n_lines--;
            g_modified = true;
        }
    }
}

static void split_line() {
    if (g_n_lines >= MAX_LINES) return;
    uint32_t row = g_cur_row;
    uint32_t col = g_cur_col;
    uint32_t len = g_line_len[row];

    for (uint32_t i = g_n_lines; i > row + 1; --i) {
        memcpy(g_lines[i], g_lines[i - 1], MAX_LINE_LEN + 1);
        g_line_len[i] = g_line_len[i - 1];
    }

    uint32_t tail = len - col;
    memcpy(g_lines[row + 1], g_lines[row] + col, tail);
    g_lines[row + 1][tail] = '\0';
    g_line_len[row + 1]    = (uint16_t)tail;

    g_lines[row][col] = '\0';
    g_line_len[row]   = (uint16_t)col;

    g_n_lines++;
    g_cur_row++;
    g_cur_col  = 0;
    g_modified = true;
}

static void start_naming(bool then_quit) {
    g_naming         = true;
    g_name_then_quit = then_quit;

    static const char def[] = "untitled.txt";
    g_name_len = 0;
    for (const char* p = def; *p && g_name_len < sizeof(g_name_buf) - 1; ++p)
        g_name_buf[g_name_len++] = *p;
    g_name_buf[g_name_len] = '\0';
    g_dirty = true;
}

static void confirm_name() {
    if (g_name_len == 0) {

        g_naming = false;
        g_dirty  = true;
        return;
    }

    const char* cwd = shell::cwd();
    uint32_t i = 0;
    if (cwd[0] && g_name_buf[0] != '/') {
        for (const char* p = cwd; *p && i < sizeof(g_path) - 2; ++p)
            g_path[i++] = *p;
        g_path[i++] = '/';
    }
    for (uint32_t j = 0; j < g_name_len && i < sizeof(g_path) - 1; ++j)
        g_path[i++] = g_name_buf[j];
    g_path[i] = '\0';

    if (g_win) {
        uint32_t k = 0;
        while (g_path[k] && k < sizeof(g_win->title) - 1)
            g_win->title[k] = g_path[k++];
        g_win->title[k] = '\0';
    }

    g_naming = false;
    g_dirty  = true;
    save_file();
    if (g_name_then_quit)
        editor::close();
}

static void naming_on_key(char c) {
    uint8_t uc = (uint8_t)c;
    if (uc == '\n' || uc == '\r') {
        confirm_name();
    } else if (uc == 0x1Bu || uc == 0x03u || uc == 0x07u) {

        g_naming = false;
        g_dirty  = true;
    } else if ((uc == '\b' || uc == 127) && g_name_len > 0) {
        g_name_buf[--g_name_len] = '\0';
        g_dirty = true;
    } else if (uc >= 0x20u && uc <= 0x7Eu && g_name_len < sizeof(g_name_buf) - 1) {
        g_name_buf[g_name_len++] = c;
        g_name_buf[g_name_len]   = '\0';
        g_dirty = true;
    }
}

}

namespace editor {

bool active() { return g_active; }

bool open(const char* path) {
    if (g_active) return false;

    memset(g_path, 0, sizeof(g_path));
    if (path && path[0]) {
        uint32_t i = 0;
        while (path[i] && i < sizeof(g_path) - 1) { g_path[i] = path[i]; ++i; }
    }

    g_win = wm::win_create(60, 50, ED_W, ED_H,
                           g_path[0] ? g_path : "editor");
    if (!g_win) return false;

    g_cur_row    = 0;
    g_cur_col    = 0;
    g_top_line   = 0;
    g_left_col   = 0;
    g_modified   = false;
    g_blink_on   = true;
    g_blink_last = 0;
    g_dirty      = true;
    g_naming     = false;
    g_name_len   = 0;
    g_name_buf[0]= '\0';

    load_file();

    g_active = true;
    do_render();
    return true;
}

void close() {
    if (!g_active) return;
    if (g_win) { wm::win_destroy(g_win); g_win = nullptr; }
    g_active   = false;
    g_modified = false;
}

void on_key(char c) {
    if (!g_active) return;

    if (g_naming) {
        naming_on_key(c);
        if (g_dirty) do_render();
        return;
    }

    uint8_t uc = (uint8_t)c;

    switch (uc) {

    case 0x80:
        if (g_cur_row > 0) { --g_cur_row; clamp_col(); }
        break;
    case 0x81:
        if (g_cur_row + 1 < g_n_lines) { ++g_cur_row; clamp_col(); }
        break;
    case 0x82:
        if (g_cur_col > 0) --g_cur_col;
        else if (g_cur_row > 0) { --g_cur_row; g_cur_col = g_line_len[g_cur_row]; }
        break;
    case 0x83:
        if (g_cur_col < g_line_len[g_cur_row]) ++g_cur_col;
        else if (g_cur_row + 1 < g_n_lines) { ++g_cur_row; g_cur_col = 0; }
        break;
    case 0x84:
        g_cur_col = 0;
        break;
    case 0x85:
        g_cur_col = g_line_len[g_cur_row];
        break;
    case 0x86:
        if (g_cur_row >= ED_TEXT_ROWS) g_cur_row -= ED_TEXT_ROWS;
        else                           g_cur_row  = 0;
        clamp_col();
        break;
    case 0x87:
        g_cur_row += ED_TEXT_ROWS;
        if (g_cur_row >= g_n_lines) g_cur_row = g_n_lines - 1;
        clamp_col();
        break;
    case 0x88:
        delete_at();
        break;

    case 0x13:
        if (!g_path[0]) { start_naming(false); break; }
        save_file();
        break;
    case 0x11:
        close();
        return;
    case 0x18:
        if (!g_path[0]) { start_naming(true); break; }
        save_file();
        close();
        return;

    case '\b':
    case 127:
        delete_before();
        break;
    case '\n':
    case '\r':
        split_line();
        break;
    case '\t':
        for (int i = 0; i < 4; ++i) insert_char(' ');
        break;
    default:
        if (uc >= 0x20u && uc <= 0x7Eu) insert_char(c);
        break;
    }

    ensure_viewport();
    g_dirty = true;
}

void tick(uint64_t ticks) {
    if (!g_active) return;

    if (g_win && g_win->close_requested) { close(); return; }
    if (ticks - g_blink_last >= BLINK_PERIOD) {
        g_blink_last = ticks;
        g_blink_on   = !g_blink_on;
        g_dirty      = true;
    }
    if (g_dirty) do_render();
}

}
