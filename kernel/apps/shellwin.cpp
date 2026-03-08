/*
  shellwin.cpp - wraps the wm terminal grid in a floating window
  opened when the shell desktop icon is double-clicked
  every tick it blits the terminal text into the window's client framebuffer
*/
#include "kernel/apps/shellwin.hpp"
#include "kernel/apps/controlpanel.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/gfx/font.hpp"
#include <stdint.h>

static uint32_t g_sw_w  = 640u;
static uint32_t g_sw_h  = 0u;
static uint32_t g_sw_ch = 400u;

static constexpr uint32_t SW_BG = 0x00000000u;

namespace {

static wm::Window* g_win    = nullptr;
static bool        g_active = false;

}

namespace shellwin {

void open() {
    if (g_active) return;

    g_sw_w  = controlpanel::app_pref_w(0);
    g_sw_ch = controlpanel::app_pref_h(0);
    g_sw_h  = g_sw_ch + wm::WIN_TITLEBAR_H;

    int32_t ox = (int32_t)((1280u - g_sw_w) / 2u);
    int32_t oy = (int32_t)((800u  - g_sw_h) / 2u);
    g_win = wm::win_create(ox, oy, g_sw_w, g_sw_h, "Shell");
    if (!g_win) return;
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

    wm::term_render_to_fb(fb, g_sw_w, g_sw_ch, SW_BG);

    wm::win_mark_dirty(g_win);
}

}
