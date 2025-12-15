#include "kernel/input/input.hpp"

namespace input
{
    // event types
    static constexpr u16 EV_KEY = 0x0001;
    static constexpr u16 EV_REL = 0x0002;
    static constexpr u16 EV_ABS = 0x0003;

    // REL codes
    static constexpr u16 REL_X = 0x0000;
    static constexpr u16 REL_Y = 0x0001;

    // ABS codes (Linux input)
    static constexpr u16 ABS_X = 0x0000;
    static constexpr u16 ABS_Y = 0x0001;

    // mouse buttons (Linux input)
    static constexpr u16 BTN_LEFT_CODE   = 0x0110;
    static constexpr u16 BTN_RIGHT_CODE  = 0x0111;
    static constexpr u16 BTN_MIDDLE_CODE = 0x0112;

    static inline s32 clamp_s32(s32 v, s32 lo, s32 hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    // Heuristic scaling for ABS:
    // - If the value already looks like pixels, use directly.
    // - Otherwise assume a common absolute range (0..32767) and scale.
    static inline s32 abs_to_px(s32 v, u32 pixels)
    {
        if (pixels == 0) return 0;

        if (v >= 0 && v < (s32)pixels)
        {
            return v;
        }

        // clamp typical tablet range
        if (v < 0) v = 0;
        if (v > 32767) v = 32767;

        // scale 0..32767 -> 0..pixels-1
        u64 num = (u64)v * (u64)(pixels - 1);
        return (s32)(num / 32767ull);
    }

    void init(State& s, u32 screen_w, u32 screen_h)
    {
        s.mouse_x = (s32)(screen_w / 2);
        s.mouse_y = (s32)(screen_h / 2);
        s.buttons = 0;

        s.last_key_code = 0;
        s.last_key_value = 0;
        s.key_changed = false;
    }

    static void handle_key(State& s, const virtio_input::Event& e)
    {
        // mouse buttons are EV_KEY too
        if (e.code == BTN_LEFT_CODE)
        {
            if (e.value) s.buttons |= BTN_LEFT;
            else         s.buttons &= ~BTN_LEFT;
            return;
        }
        if (e.code == BTN_RIGHT_CODE)
        {
            if (e.value) s.buttons |= BTN_RIGHT;
            else         s.buttons &= ~BTN_RIGHT;
            return;
        }
        if (e.code == BTN_MIDDLE_CODE)
        {
            if (e.value) s.buttons |= BTN_MIDDLE;
            else         s.buttons &= ~BTN_MIDDLE;
            return;
        }

        // keyboard key
        s.last_key_code = e.code;
        s.last_key_value = e.value;
        s.key_changed = true;
    }

    static void handle_rel(State& s, const virtio_input::Event& e, u32 screen_w, u32 screen_h)
    {
        if (e.code == REL_X) s.mouse_x += (s32)e.value;
        if (e.code == REL_Y) s.mouse_y += (s32)e.value;

        s.mouse_x = clamp_s32(s.mouse_x, 0, (s32)screen_w - 1);
        s.mouse_y = clamp_s32(s.mouse_y, 0, (s32)screen_h - 1);
    }

    static void handle_abs(State& s, const virtio_input::Event& e, u32 screen_w, u32 screen_h)
    {
        if (e.code == ABS_X) s.mouse_x = abs_to_px((s32)e.value, screen_w);
        if (e.code == ABS_Y) s.mouse_y = abs_to_px((s32)e.value, screen_h);

        s.mouse_x = clamp_s32(s.mouse_x, 0, (s32)screen_w - 1);
        s.mouse_y = clamp_s32(s.mouse_y, 0, (s32)screen_h - 1);
    }

    void poll_device(virtio_input::Device& dev, State& s, u32 screen_w, u32 screen_h)
    {
        virtio_input::Event e {};
        while (dev.poll(e))
        {
            if (e.type == EV_KEY) handle_key(s, e);
            else if (e.type == EV_REL) handle_rel(s, e, screen_w, screen_h);
            else if (e.type == EV_ABS) handle_abs(s, e, screen_w, screen_h);
        }
    }
}
