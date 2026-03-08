/*
  desktop.cpp - icon grid on the desktop wallpaper
  renders 48x48 icons for each app in a vertical column
  single-click selects (highlight), double-click launches the app
  also handles start menu launch dispatch from main.cpp
*/
#include "kernel/apps/desktop.hpp"
#include "kernel/apps/editor.hpp"
#include "kernel/apps/controlpanel.hpp"
#include "kernel/apps/shellwin.hpp"
#include "kernel/apps/calc.hpp"
#include "kernel/apps/fileexplorer.hpp"
#include "kernel/apps/sysmon.hpp"
#include "kernel/apps/paint.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/irq/timer.hpp"
#include "kernel/gfx/assets/icon_shell.hpp"
#include "kernel/gfx/assets/icon_editor.hpp"
#include "kernel/gfx/assets/icon_controlpanel.hpp"
#include "kernel/gfx/assets/icon_calc.hpp"
#include "kernel/gfx/assets/icon_files.hpp"
#include "kernel/gfx/assets/icon_sysmon.hpp"
#include "kernel/gfx/assets/icon_paint.hpp"
#include "kernel/wm/wm.hpp"

static constexpr uint32_t ICON_IMG_W      = 48u;
static constexpr uint32_t ICON_IMG_H      = 48u;
static constexpr uint32_t ICON_CELL_W     = 80u;
static constexpr uint32_t ICON_CELL_H     = 80u;
static constexpr uint32_t ICON_IMG_OFF_X  = (ICON_CELL_W - ICON_IMG_W) / 2;
static constexpr uint32_t ICON_IMG_OFF_Y  = 8u;
static constexpr uint32_t ICON_LBL_OFF_Y  = ICON_IMG_OFF_Y + ICON_IMG_H + 6u;

static constexpr uint32_t GRID_LEFT       = 12u;

static uint32_t g_grid_top = 0;

namespace {

enum class AppID { Shell, Editor, ControlPanel, Calculator, Files, SysMonitor, Paint };

struct DesktopIcon {
    const uint32_t* sprite;
    const char*     label;
    AppID           id;
};

static const DesktopIcon k_icons[] = {
    { gfx::assets::icon_shell,        "Shell",         AppID::Shell        },
    { gfx::assets::icon_editor,       "Text Editor",   AppID::Editor       },
    { gfx::assets::icon_controlpanel, "Control Panel", AppID::ControlPanel },
    { gfx::assets::icon_files,        "Files",         AppID::Files        },
    { gfx::assets::icon_sysmon,       "System Monitor",  AppID::SysMonitor   },
    { gfx::assets::icon_paint,        "Paint",         AppID::Paint        },
};

static constexpr int ICON_COUNT = 6;

static constexpr uint32_t LBL_FG = 0x00FFFFFFu;

static bool g_shell_open_pending = false;

static constexpr uint32_t SEL_BG    = 0x001E4FC2u;
static constexpr uint32_t SEL_RING  = 0x006699FFu;
static constexpr uint32_t SEL_FG    = 0x00FFFFFFu;

static constexpr uint64_t DBLCLICK_TICKS = 50u;

static int      g_selected        = -1;
static int      g_last_click_icon = -1;
static uint64_t g_last_click_tick =  0u;

static uint32_t icon_x()      { return GRID_LEFT + ICON_IMG_OFF_X; }
static uint32_t icon_y(int i) { return g_grid_top + ICON_IMG_OFF_Y + (uint32_t)i * ICON_CELL_H; }
static uint32_t label_x_centre(const char* lbl) {
    uint32_t len = 0;
    while (lbl[len]) ++len;
    uint32_t label_px = len * gfx::FONT_W;

    if (label_px >= ICON_CELL_W)
        return GRID_LEFT;
    return GRID_LEFT + (ICON_CELL_W - label_px) / 2;
}

static bool hit_icon(int i, int32_t px, int32_t py) {
    int32_t cx = (int32_t)GRID_LEFT;
    int32_t cy = (int32_t)(g_grid_top + (uint32_t)i * ICON_CELL_H);
    return px >= cx && px < cx + (int32_t)ICON_CELL_W &&
           py >= cy && py < cy + (int32_t)ICON_CELL_H;
}

}

namespace desktop {

void init() {
    g_grid_top = wm::WM_TITLEBAR_H + 10u;
}

void render() {
    uint32_t bg = wm::get_wallpaper_color();
    for (int i = 0; i < ICON_COUNT; ++i) {
        const DesktopIcon& ic = k_icons[i];
        bool sel = (i == g_selected);

        uint32_t cell_x = GRID_LEFT;
        uint32_t cell_y = g_grid_top + (uint32_t)i * ICON_CELL_H;

        if (sel) {
            gfx::fill_rect(cell_x, cell_y, ICON_CELL_W, ICON_CELL_H, SEL_BG);
            gfx::draw_rect(cell_x, cell_y, ICON_CELL_W, ICON_CELL_H, SEL_RING);
        }

        uint32_t img_bg = sel ? SEL_BG : bg;
        gfx::blit_alpha(ic.sprite, img_bg, icon_x(), icon_y(i), ICON_IMG_W, ICON_IMG_H);

        uint32_t lx  = label_x_centre(ic.label);
        uint32_t ly  = g_grid_top + ICON_LBL_OFF_Y + (uint32_t)i * ICON_CELL_H;
        uint32_t fg  = sel ? SEL_FG : LBL_FG;
        uint32_t lbg = sel ? SEL_BG : bg;
        gfx::draw_text(lx, ly, ic.label, fg, lbg);
    }
}

static void launch_icon(int i) {
    switch (k_icons[i].id) {
        case AppID::Shell:
            g_shell_open_pending = true;
            shellwin::open();
            break;
        case AppID::Editor:
            if (!editor::active())
                editor::open(nullptr);
            break;
        case AppID::ControlPanel:
            if (!controlpanel::active())
                controlpanel::open();
            break;
        case AppID::Calculator:
            if (!calc::active())
                calc::open();
            break;
        case AppID::Files:
            if (!fileexplorer::active())
                fileexplorer::open();
            break;
        case AppID::SysMonitor:
            if (!sysmon::active())
                sysmon::open();
            break;
        case AppID::Paint:
            if (!paint::active())
                paint::open();
            break;
    }
}

void on_click(int32_t x, int32_t y) {
    for (int i = 0; i < ICON_COUNT; ++i) {
        if (!hit_icon(i, x, y)) continue;

        uint64_t now = timer::ticks();
        bool dbl = (g_last_click_icon == i) &&
                   (now - g_last_click_tick <= DBLCLICK_TICKS);

        g_last_click_icon = i;
        g_last_click_tick = now;

        if (dbl) {

            g_selected = -1;
            launch_icon(i);
        } else {

            g_selected = i;
        }
        return;
    }

    g_selected = -1;
}

uint32_t icon_zone_term_rows() {

    return 0;
}

bool shell_was_opened() {
    bool b = g_shell_open_pending;
    g_shell_open_pending = false;
    return b;
}

void launch_app(int start_menu_idx) {

    switch (start_menu_idx) {
        case 0:
            g_shell_open_pending = true;
            shellwin::open();
            break;
        case 1:
            if (!editor::active()) editor::open(nullptr);
            break;
        case 2:
            if (!controlpanel::active()) controlpanel::open();
            break;
        case 3:
            if (!calc::active()) calc::open();
            break;
        case 4:
            if (!fileexplorer::active()) fileexplorer::open();
            break;
        case 5:
            if (!sysmon::active()) sysmon::open();
            break;
        case 6:
            if (!paint::active()) paint::open();
            break;
    }
}

bool calc_is_active()         { return calc::active(); }
bool fileexplorer_is_active() { return fileexplorer::active(); }
bool sysmon_is_active()       { return sysmon::active(); }
bool paint_is_active()        { return paint::active(); }

}
