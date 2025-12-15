#pragma once
#include "types.hpp"

#include "drivers/virtio/input/virtio_input.hpp"

namespace input
{
    enum Buttons : u32
    {
        BTN_LEFT   = 1u << 0,
        BTN_RIGHT  = 1u << 1,
        BTN_MIDDLE = 1u << 2,
    };

    struct State
    {
        s32 mouse_x {};
        s32 mouse_y {};
        u32 buttons {};

        u16 last_key_code {};
        s32 last_key_value {}; // 1 press, 0 release, 2 repeat
        bool key_changed {};
    };

    void init(State& s, u32 screen_w, u32 screen_h);

    // Call this repeatedly; it will poll ONE virtio input device and update state.
    void poll_device(virtio_input::Device& dev, State& s, u32 screen_w, u32 screen_h);
}
