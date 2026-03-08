/*
  wm.hpp - window manager public interface
  win_create/win_destroy, mouse_update, render, render_dirty
  also exposes the terminal text layer, start menu, wallpaper color, and desktop click events
*/
#pragma once
#include <stdint.h>

namespace wm {

static constexpr uint32_t WM_TITLEBAR_H   = 28u;
static constexpr uint32_t WIN_TITLEBAR_H  = 26u;
static constexpr uint32_t TASKBAR_H       = 36u;
static constexpr uint32_t MAX_WINDOWS     =  8u;
static constexpr uint32_t WIN_CLOSE_BTN_W = 20u;
static constexpr uint32_t WIN_MAX_BTN_W   = 20u;
static constexpr uint32_t START_BTN_W     = 96u;

struct Window {
    int32_t  x, y;
    uint32_t w, h;
    uint32_t* client_fb;
    uint32_t  client_h;
    char  title[32];
    bool  dirty;
    bool  visible;

    bool    close_requested;
    bool    client_clicked;
    int32_t click_cx, click_cy;
    bool    right_clicked;
    int32_t right_cx, right_cy;

    bool    client_held;
    int32_t held_cx, held_cy;

    bool     maximized;
    int32_t  restore_x, restore_y;
    uint32_t restore_w, restore_h;
    uint32_t fb_w;
    uint32_t fb_client_h;
};

void init(uint32_t screen_w, uint32_t screen_h);

Window* win_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                   const char* title);

void win_destroy(Window* win);

void win_mark_dirty(Window* win);

void mouse_update(int32_t abs_x, int32_t abs_y, bool btn_left, bool btn_right = false);

void set_status(const char* s);

void term_putc(char c);
void term_puts(const char* s);
void term_clear();

void render();
void render_dirty();

uint32_t term_cols();
uint32_t term_rows();
void     term_set_cursor(uint32_t col, uint32_t row);

void     set_wallpaper_color(uint32_t bgra);
uint32_t get_wallpaper_color();

void set_terminal_visible(bool visible);

void term_render_to_fb(uint32_t* dst, uint32_t dst_w, uint32_t dst_h, uint32_t bg);

int     win_count();
Window* win_get(int idx);

bool    desktop_was_clicked();
int32_t desktop_click_x();
int32_t desktop_click_y();

bool start_app_was_selected(int& app_idx);

bool start_menu_wants_keys();
void start_menu_on_key(char c);

}
