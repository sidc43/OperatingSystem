/*
  calc.cpp - 4-function floating-point calculator in a wm window
  has a display area, pending operator row, and a 4x5 button grid
  supports mouse clicks and keyboard input (digits, + - * / enter escape backspace %)
*/
#include "kernel/apps/calc.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/gfx/draw.hpp"
#include <stdint.h>
#include <string.h>

static constexpr uint32_t CALC_W       = 278u;
static constexpr uint32_t CALC_CLIENT_H = 310u;
static constexpr uint32_t CALC_H       = CALC_CLIENT_H + wm::WIN_TITLEBAR_H;
static constexpr uint32_t FW           = CALC_W;

static constexpr uint32_t PAD         =  8u;
static constexpr uint32_t DISP_X      = PAD;
static constexpr uint32_t DISP_Y      = PAD;
static constexpr uint32_t DISP_W      = CALC_W - 2u * PAD;
static constexpr uint32_t DISP_H      = 32u;
static constexpr uint32_t OPER_Y      = DISP_Y + DISP_H + 2u;
static constexpr uint32_t OPER_H      = gfx::FONT_H;
static constexpr uint32_t BTN_Y0      = OPER_Y + OPER_H + 4u;
static constexpr uint32_t BTN_COLS    =  4u;
static constexpr uint32_t BTN_ROWS    =  5u;
static constexpr uint32_t BTN_GAP     =  4u;
static constexpr uint32_t BTN_W       = (CALC_W - 2u*PAD - (BTN_COLS-1u)*BTN_GAP) / BTN_COLS;
static constexpr uint32_t BTN_H       = (CALC_CLIENT_H - BTN_Y0 - PAD - (BTN_ROWS-1u)*BTN_GAP) / BTN_ROWS;

static constexpr uint32_t C_BG        = 0x00C0C0C0u;
static constexpr uint32_t C_DISP_BG   = 0x00000000u;
static constexpr uint32_t C_DISP_FG   = 0x0000FF00u;
static constexpr uint32_t C_DISP_ERR  = 0x000000FFu;
static constexpr uint32_t C_BTN_NUM   = 0x00C0C0C0u;
static constexpr uint32_t C_BTN_OP    = 0x00A0D8FFu;
static constexpr uint32_t C_BTN_CLR   = 0x00A0A0FFu;
static constexpr uint32_t C_BTN_EQ    = 0x0060C060u;
static constexpr uint32_t C_BTN_FG    = 0x00000000u;
static constexpr uint32_t C_HI        = 0x00FFFFFFu;
static constexpr uint32_t C_SHADOW    = 0x00404040u;
static constexpr uint32_t C_OPROW_BG  = 0x00C0C0C0u;
static constexpr uint32_t C_OPROW_FG  = 0x00000080u;

enum BtnKind { BK_NUM, BK_OP, BK_CLR, BK_EQ };

struct Btn {
    const char* label;
    char        key;
    BtnKind     kind;
};

static const Btn k_btns[BTN_ROWS][BTN_COLS] = {
    { {"CE", 0, BK_CLR}, {"C",  'c', BK_CLR}, {"BS", '\b', BK_CLR}, {"/",  '/', BK_OP} },
    { {"7",  '7', BK_NUM}, {"8",  '8', BK_NUM}, {"9",  '9', BK_NUM}, {"*",  '*', BK_OP} },
    { {"4",  '4', BK_NUM}, {"5",  '5', BK_NUM}, {"6",  '6', BK_NUM}, {"-",  '-', BK_OP} },
    { {"1",  '1', BK_NUM}, {"2",  '2', BK_NUM}, {"3",  '3', BK_NUM}, {"+",  '+', BK_OP} },
    { {"+/-", 0, BK_OP},   {"0",  '0', BK_NUM}, {".",  '.', BK_NUM}, {"=",  '=', BK_EQ} },
};

static uint32_t btn_color(BtnKind k) {
    switch (k) {
        case BK_OP:  return C_BTN_OP;
        case BK_CLR: return C_BTN_CLR;
        case BK_EQ:  return C_BTN_EQ;
        default:     return C_BTN_NUM;
    }
}

namespace {

static wm::Window* g_win    = nullptr;
static bool        g_dirty  = true;

static constexpr uint32_t DISP_MAXLEN = 20u;
static char   g_display[DISP_MAXLEN + 1] = "0";
static double g_accum  = 0.0;
static char   g_op     = 0;
static bool   g_fresh  = true;
static bool   g_error  = false;

static double calc_abs(double v) { return v < 0.0 ? -v : v; }

static double calc_floor(double v) {
    int64_t i = (int64_t)v;
    if ((double)i > v) --i;
    return (double)i;
}

static void dtoa(double v, char* out, uint32_t maxlen) {
    if (maxlen == 0) { return; }
    uint32_t pos = 0;

#define DTOA_PUT(ch) do { if (pos < maxlen) out[pos++] = (ch); } while (0)

    if (v < 0.0) { DTOA_PUT('-'); v = -v; }

    if (v >= 1e15) {
        const char* s = "Overflow";
        while (*s && pos < maxlen) out[pos++] = *s++;
        if (pos < maxlen) out[pos] = '\0';
        return;
    }

    double int_part_d = calc_floor(v + 1e-12);
    double frac = v - int_part_d;
    if (frac < 0.0) frac = 0.0;
    int64_t int_part = (int64_t)int_part_d;

    if (int_part == 0) {
        DTOA_PUT('0');
    } else {
        char tmp[20]; int n = 0;
        int64_t iv = int_part;
        while (iv > 0) { tmp[n++] = (char)('0' + (int)(iv % 10)); iv /= 10; }
        for (int i = n-1; i >= 0; --i) DTOA_PUT(tmp[i]);
    }

    if (frac > 1e-10) {
        DTOA_PUT('.');
        char frac_digits[9];
        int flen = 0;
        double f = frac;
        for (int i = 0; i < 8; ++i) {
            f *= 10.0;
            int d = (int)calc_floor(f + 1e-9);
            if (d < 0) d = 0;
            if (d > 9) d = 9;
            frac_digits[flen++] = (char)('0' + d);
            f -= (double)d;
            if (f < 0.0) f = 0.0;
        }

        while (flen > 1 && frac_digits[flen-1] == '0') --flen;
        for (int i = 0; i < flen; ++i) DTOA_PUT(frac_digits[i]);
    }

    out[pos] = '\0';
#undef DTOA_PUT
}

static double parse_display() {
    double result = 0.0;
    bool neg = false;
    const char* p = g_display;
    if (*p == '-') { neg = true; ++p; }
    while (*p >= '0' && *p <= '9') { result = result * 10.0 + (*p - '0'); ++p; }
    if (*p == '.') {
        ++p;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') {
            result += (*p - '0') * scale;
            scale  *= 0.1;
            ++p;
        }
    }
    return neg ? -result : result;
}

static void display_value(double v) {
    dtoa(v, g_display, DISP_MAXLEN);
    if (g_display[0] == '\0') { g_display[0] = '0'; g_display[1] = '\0'; }
}

static void do_equals() {
    if (g_op == 0) return;
    double rhs = parse_display();
    double result = g_accum;
    switch (g_op) {
        case '+': result = g_accum + rhs; break;
        case '-': result = g_accum - rhs; break;
        case '*': result = g_accum * rhs; break;
        case '/':
            if (calc_abs(rhs) < 1e-15) { g_error = true; g_op = 0; return; }
            result = g_accum / rhs;
            break;
    }
    g_op = 0;
    g_accum = result;
    g_error = false;
    display_value(result);
    g_fresh = true;
}

static void fb_fill(uint32_t* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    for (uint32_t dy = 0; dy < h && (y+dy) < CALC_CLIENT_H; ++dy)
        for (uint32_t dx = 0; dx < w && (x+dx) < FW; ++dx)
            fb[(y+dy)*FW + (x+dx)] = c;
}

static void fb_char(uint32_t* fb, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    gfx::draw_char_into(fb, FW, x, y, c, fg, bg);
}

static void fb_text(uint32_t* fb, uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg) {
    for (; *s; ++s, x += gfx::FONT_W)
        fb_char(fb, x, y, *s, fg, bg);
}

static void fb_text_right(uint32_t* fb, uint32_t field_x, uint32_t y,
                           uint32_t field_w, const char* s,
                           uint32_t fg, uint32_t bg) {
    uint32_t len = 0;
    while (s[len]) ++len;
    uint32_t text_px = len * gfx::FONT_W;
    uint32_t x = (text_px < field_w) ? (field_x + field_w - text_px) : field_x;
    fb_text(fb, x, y, s, fg, bg);
}

static void draw_button(uint32_t* fb, uint32_t col, uint32_t row, bool pressed) {
    uint32_t x = PAD + col * (BTN_W + BTN_GAP);
    uint32_t y = BTN_Y0 + row * (BTN_H + BTN_GAP);
    const Btn& b = k_btns[row][col];
    uint32_t fill = pressed ? 0x00909090u : btn_color(b.kind);
    fb_fill(fb, x, y, BTN_W, BTN_H, fill);

    for (uint32_t i = 0; i < BTN_W; ++i) {
        if (y < CALC_CLIENT_H)          fb[(y)*FW + x+i]       = pressed ? C_SHADOW : C_HI;
        if (y+BTN_H-1 < CALC_CLIENT_H)  fb[(y+BTN_H-1)*FW+x+i] = pressed ? C_HI : C_SHADOW;
    }
    for (uint32_t i = 1; i+1 < BTN_H; ++i) {
        if (y+i < CALC_CLIENT_H) {
            fb[(y+i)*FW + x]           = pressed ? C_SHADOW : C_HI;
            fb[(y+i)*FW + x+BTN_W-1]   = pressed ? C_HI : C_SHADOW;
        }
    }

    uint32_t llen = 0; while (b.label[llen]) ++llen;
    uint32_t lx = x + (BTN_W > llen*gfx::FONT_W ? (BTN_W - llen*gfx::FONT_W)/2u : 0u);
    uint32_t ly = y + (BTN_H > gfx::FONT_H       ? (BTN_H - gfx::FONT_H)/2u : 0u);
    fb_text(fb, lx, ly, b.label, C_BTN_FG, fill);
}

static void redraw() {
    if (!g_win || !g_win->client_fb) return;
    uint32_t* fb = g_win->client_fb;

    fb_fill(fb, 0, 0, CALC_W, CALC_CLIENT_H, C_BG);

    fb_fill(fb, DISP_X, DISP_Y, DISP_W, DISP_H, C_DISP_BG);

    for (uint32_t i = 0; i < DISP_W; ++i) {
        fb[(DISP_Y)*FW + DISP_X+i]         = C_SHADOW;
        fb[(DISP_Y+DISP_H-1)*FW + DISP_X+i] = C_HI;
    }
    for (uint32_t i = 1; i+1 < DISP_H; ++i) {
        fb[(DISP_Y+i)*FW + DISP_X]            = C_SHADOW;
        fb[(DISP_Y+i)*FW + DISP_X+DISP_W-1]   = C_HI;
    }

    uint32_t dtext_y = DISP_Y + (DISP_H > gfx::FONT_H ? (DISP_H - gfx::FONT_H)/2u : 0u);
    uint32_t dtext_fg = g_error ? C_DISP_ERR : C_DISP_FG;
    const char* disp_str = g_error ? "Error" : g_display;
    fb_text_right(fb, DISP_X+4u, dtext_y, DISP_W - 8u, disp_str, dtext_fg, C_DISP_BG);

    fb_fill(fb, 0, OPER_Y, CALC_W, OPER_H, C_OPROW_BG);
    if (g_op != 0) {

        char expr[DISP_MAXLEN + 4];
        char accstr[DISP_MAXLEN + 1];
        dtoa(g_accum, accstr, DISP_MAXLEN);

        uint32_t elen = 0;
        for (uint32_t i = 0; accstr[i] && elen < DISP_MAXLEN; ++i) expr[elen++] = accstr[i];

        expr[elen++] = ' ';
        expr[elen++] = g_op;
        expr[elen] = '\0';

        fb_text_right(fb, DISP_X + 4u, OPER_Y, DISP_W - 8u, expr, C_OPROW_FG, C_OPROW_BG);
    }

    for (uint32_t row = 0; row < BTN_ROWS; ++row)
        for (uint32_t col = 0; col < BTN_COLS; ++col)
            draw_button(fb, col, row, false);

    wm::win_mark_dirty(g_win);
}

static void press_button(uint32_t col, uint32_t row) {
    if (row >= BTN_ROWS || col >= BTN_COLS) return;
    const Btn& b = k_btns[row][col];
    char k = b.key;

    if (k >= '0' && k <= '9') {
        if (g_error) return;
        if (g_fresh) {
            g_display[0] = k; g_display[1] = '\0';
            g_fresh = false;
        } else {
            uint32_t len = 0; while (g_display[len]) ++len;
            if (len < DISP_MAXLEN - 1) {

                if (len == 1 && g_display[0] == '0') {
                    g_display[0] = k; g_display[1] = '\0';
                } else {
                    g_display[len] = k; g_display[len+1] = '\0';
                }
            }
        }
        g_dirty = true;
        return;
    }

    if (k == '.') {
        if (g_error) return;
        if (g_fresh) {
            g_display[0] = '0'; g_display[1] = '.'; g_display[2] = '\0';
            g_fresh = false;
        } else {

            bool has_dot = false;
            for (int i = 0; g_display[i]; ++i) if (g_display[i] == '.') { has_dot = true; break; }
            if (!has_dot) {
                uint32_t len = 0; while (g_display[len]) ++len;
                if (len < DISP_MAXLEN - 1) {
                    g_display[len] = '.'; g_display[len+1] = '\0';
                }
            }
        }
        g_dirty = true;
        return;
    }

    if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (g_error) return;
        if (g_op != 0) do_equals();
        g_accum = parse_display();
        g_op = k;
        g_fresh = true;
        g_dirty = true;
        return;
    }

    if (k == '=') {
        if (g_error) { g_error = false; g_fresh = true; g_display[0]='0'; g_display[1]='\0'; g_op=0; g_dirty=true; return; }
        do_equals();
        g_dirty = true;
        return;
    }

    if (k == '\b') {
        if (g_error || g_fresh) return;
        uint32_t len = 0; while (g_display[len]) ++len;
        if (len > 1) { g_display[len-1] = '\0'; }
        else { g_display[0] = '0'; g_fresh = false; }
        g_dirty = true;
        return;
    }

    if (k == 'c') {
        g_display[0] = '0'; g_display[1] = '\0';
        g_fresh = true; g_error = false;
        g_dirty = true;
        return;
    }

    if (b.label[0] == 'C' && b.label[1] == 'E') {
        g_display[0] = '0'; g_display[1] = '\0';
        g_accum = 0.0; g_op = 0; g_fresh = true; g_error = false;
        g_dirty = true;
        return;
    }

    if (b.label[0] == '+' && b.label[1] == '/') {
        if (!g_error) {
            if (g_display[0] == '-') {

                uint32_t len = 0; while (g_display[len]) ++len;
                for (uint32_t i = 0; i < len; ++i) g_display[i] = g_display[i+1];

            } else {

                uint32_t len = 0; while (g_display[len]) ++len;
                if (len < DISP_MAXLEN - 1) {
                    g_display[len + 1] = '\0';
                    for (uint32_t i = len; i > 0; --i) g_display[i] = g_display[i-1];
                    g_display[0] = '-';
                }
            }
            g_dirty = true;
        }
        return;
    }
}

}

namespace calc {

void open() {
    if (g_win) return;
    uint32_t sw = 1280u, sh = 800u;
    (void)sw; (void)sh;
    int32_t wx = 360, wy = 120;
    g_win = wm::win_create(wx, wy, CALC_W, CALC_H, "Calculator");
    if (!g_win) return;

    g_display[0] = '0'; g_display[1] = '\0';
    g_accum = 0.0; g_op = 0; g_fresh = true; g_error = false;
    g_dirty = true;
    redraw();
}

void close() {
    if (!g_win) return;
    wm::win_destroy(g_win);
    g_win = nullptr;
}

bool active() { return g_win != nullptr; }

void on_key(char c) {
    if (!g_win) return;
    switch (c) {
        case '0': press_button(1,4); break;
        case '1': press_button(0,3); break;
        case '2': press_button(1,3); break;
        case '3': press_button(2,3); break;
        case '4': press_button(0,2); break;
        case '5': press_button(1,2); break;
        case '6': press_button(2,2); break;
        case '7': press_button(0,1); break;
        case '8': press_button(1,1); break;
        case '9': press_button(2,1); break;
        case '.': press_button(2,4); break;
        case '+': press_button(3,3); break;
        case '-': press_button(3,2); break;
        case '*': press_button(3,1); break;
        case '/': press_button(3,0); break;
        case '=': case '\r': press_button(3,4); break;
        case '\b': press_button(2,0); break;
        case 'c': case 'C': press_button(1,0); break;
        case '%': {
            if (!g_error) {
                double v = parse_display();
                v /= 100.0;
                display_value(v);
                g_fresh = true;
                g_dirty = true;
            }
            break;
        }
        case 0x1b: close(); break;
        default: break;
    }
}

void tick(uint64_t ) {
    if (!g_win) return;

    if (g_win->close_requested) { close(); return; }

    if (g_win->client_clicked) {
        g_win->client_clicked = false;
        int32_t cx = g_win->click_cx;
        int32_t cy = g_win->click_cy;

        bool hit = false;
        for (uint32_t row = 0; row < BTN_ROWS && !hit; ++row) {
            for (uint32_t col = 0; col < BTN_COLS && !hit; ++col) {
                int32_t bx = (int32_t)(PAD + col * (BTN_W + BTN_GAP));
                int32_t by = (int32_t)(BTN_Y0 + row * (BTN_H + BTN_GAP));
                if (cx >= bx && cx < bx + (int32_t)BTN_W &&
                    cy >= by && cy < by + (int32_t)BTN_H) {
                    press_button(col, row);
                    hit = true;
                }
            }
        }
    }

    if (g_dirty) {
        g_dirty = false;
        redraw();
    }
}

}
