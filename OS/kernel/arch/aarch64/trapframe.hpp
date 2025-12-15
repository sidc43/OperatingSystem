#pragma once
#include "types.hpp"

namespace arch
{
    // Matches vectors.S layout:
    // main frame = 272 bytes:
    //   x0-x15 at 0..127
    //   x18-x29 at 128..223
    //   x30 at 224
    // plus pre-frame 16 bytes (x16/x17) located above the main frame.
    //
    // We treat "frame pointer" as the base of the 272-byte main frame.

    struct TrapFrame
    {
        u64 x[16];          // x0..x15
        u64 x18;            // 128
        u64 x19;
        u64 x20;
        u64 x21;
        u64 x22;
        u64 x23;
        u64 x24;
        u64 x25;
        u64 x26;
        u64 x27;
        u64 x28;
        u64 x29;
        u64 x30;            // 224
        u64 _pad[5];        // to make 272 bytes total (272 - 232 = 40 bytes = 5 u64)
    };

    static constexpr u64 MAIN_FRAME_SIZE = 272;
    static constexpr u64 FULL_FRAME_SIZE = 288; // 272 + 16 (x16/x17 pre-frame)
}
