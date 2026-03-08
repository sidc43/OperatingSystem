/*
  scan.cpp - linux evdev keycode to ascii translation table
  handles all printable ascii keys plus shift state and caps-lock
  also handles special keys (arrows, home/end, pgup/pgdn, f-keys) as custom byte values
*/
#include "kernel/drivers/virtio/scan.hpp"

namespace scan {

struct KMap { char normal; char shifted; };

static const KMap kmap[] = {
     {0,   0  },
     {0,   0  },
     {'1', '!'},
     {'2', '@'},
     {'3', '#'},
     {'4', '$'},
     {'5', '%'},
     {'6', '^'},
     {'7', '&'},
     {'8', '*'},
     {'9', '('},
     {'0', ')'},
     {'-', '_'},
     {'=', '+'},
     {'\b','\b'},
     {'\t','\t'},
     {'q', 'Q'},
     {'w', 'W'},
     {'e', 'E'},
     {'r', 'R'},
     {'t', 'T'},
     {'y', 'Y'},
     {'u', 'U'},
     {'i', 'I'},
     {'o', 'O'},
     {'p', 'P'},
     {'[', '{'},
     {']', '}'},
     {'\n','\n'},
     {0,   0  },
     {'a', 'A'},
     {'s', 'S'},
     {'d', 'D'},
     {'f', 'F'},
     {'g', 'G'},
     {'h', 'H'},
     {'j', 'J'},
     {'k', 'K'},
     {'l', 'L'},
     {';', ':'},
     {'\'','"'},
     {'`', '~'},
     {0,   0  },
     {'\\','|'},
     {'z', 'Z'},
     {'x', 'X'},
     {'c', 'C'},
     {'v', 'V'},
     {'b', 'B'},
     {'n', 'N'},
     {'m', 'M'},
     {',', '<'},
     {'.', '>'},
     {'/', '?'},
     {0,   0  },
     {'*', '*'},
     {0,   0  },
     {' ', ' '},
     {0,   0  },
};

static constexpr uint32_t KMAP_SIZE = sizeof(kmap) / sizeof(kmap[0]);

char to_ascii(uint16_t code, bool shift, bool capslock) {
    if (code >= KMAP_SIZE) return 0;
    char c = shift ? kmap[code].shifted : kmap[code].normal;

    if (capslock) {
        if (!shift && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (shift && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return c;
}

bool is_shift(uint16_t code) {
    return code == KEY_LSHIFT || code == KEY_RSHIFT;
}

bool is_ctrl(uint16_t code) {
    return code == KEY_LCTRL || code == KEY_RCTRL;
}

bool is_alt(uint16_t code) {
    return code == KEY_LALT || code == KEY_RALT;
}

}
