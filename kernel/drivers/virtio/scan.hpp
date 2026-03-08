/*
  scan.hpp - evdev keycode to ascii translation interface
  to_ascii(code, shift, capslock) returns the ascii char or 0 for non-printable
  is_shift/is_ctrl/is_alt helpers, and key code constants
*/
#pragma once
#include <stdint.h>

namespace scan {

char to_ascii(uint16_t code, bool shift, bool capslock = false);

bool is_shift(uint16_t code);
bool is_ctrl (uint16_t code);
bool is_alt  (uint16_t code);

static constexpr uint16_t KEY_ESC       = 1;
static constexpr uint16_t KEY_BACKSPACE = 14;
static constexpr uint16_t KEY_TAB       = 15;
static constexpr uint16_t KEY_ENTER     = 28;
static constexpr uint16_t KEY_LCTRL     = 29;
static constexpr uint16_t KEY_LSHIFT    = 42;
static constexpr uint16_t KEY_RSHIFT    = 54;
static constexpr uint16_t KEY_LALT      = 56;
static constexpr uint16_t KEY_CAPSLOCK  = 58;
static constexpr uint16_t KEY_UP        = 103;
static constexpr uint16_t KEY_LEFT      = 105;
static constexpr uint16_t KEY_RIGHT     = 106;
static constexpr uint16_t KEY_DOWN      = 108;
static constexpr uint16_t KEY_RCTRL     = 97;
static constexpr uint16_t KEY_RALT      = 100;
static constexpr uint16_t KEY_HOME      = 102;
static constexpr uint16_t KEY_PAGEUP    = 104;
static constexpr uint16_t KEY_END       = 107;
static constexpr uint16_t KEY_PAGEDOWN  = 109;
static constexpr uint16_t KEY_DELETE    = 111;

}
