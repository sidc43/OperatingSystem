#include "kernel/tests/tests.hpp"
#include "drivers/tty/tty.hpp"
#include "drivers/console/console.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/assert.hpp"
#include "kernel/irq/irq.hpp"
#include "drivers/interrupt/gicv2/gicv2.hpp"
#include "drivers/timer/arch_timer.hpp"
#include "kernel/mm/phys/page_alloc.hpp"
#include "src/arch/aarch64/cpu/mmu/mmu.hpp"
#include "kernel/mm/heap/kheap.hpp"
#include "kernel/mm/vm/vm.hpp"
#include "drivers/virtio/gpu/virtio_gpu.hpp"
#include "drivers/video/gfx.hpp"
#include "drivers/virtio/input/virtio_input.hpp"
#include "kernel/input/input.hpp"
#include "kernel/gui/terminal.hpp"

namespace
{
    void idle()
    {
        while (true)
        {
            asm volatile("wfe");
        }
    }

    void print_s32(int v)
    {
        if (v < 0)
        {
            kprint::puts("-");
            kprint::dec_u64((u64)(-v));
        }
        else
        {
            kprint::dec_u64((u64)v);
        }
    }

    const char* ev_type_name(u16 t)
    {
        // Linux input-style event types
        if (t == 0x0000) return "EV_SYN";
        if (t == 0x0001) return "EV_KEY";
        if (t == 0x0002) return "EV_REL";
        if (t == 0x0003) return "EV_ABS";
        return "EV_???";
    }

    static inline u32 stride_words()
    {
        return gfx::stride_bytes() / 4;
    }

    void fill_rect(u32* fb, u32 stride_w, u32 w, u32 h,
                   s32 x, s32 y, u32 rw, u32 rh, u32 color)
    {
        if (rw == 0 || rh == 0) return;

        s32 x0 = x;
        s32 y0 = y;
        s32 x1 = x + (s32)rw;
        s32 y1 = y + (s32)rh;

        if (x1 <= 0 || y1 <= 0) return;
        if (x0 >= (s32)w || y0 >= (s32)h) return;

        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (s32)w) x1 = (s32)w;
        if (y1 > (s32)h) y1 = (s32)h;

        for (s32 yy = y0; yy < y1; yy++)
        {
            u32* row = fb + (u32)yy * stride_w + (u32)x0;
            for (s32 xx = x0; xx < x1; xx++)
            {
                *row++ = color;
            }
        }

        gfx::mark_dirty((u32)x0, (u32)y0, (u32)(x1 - x0), (u32)(y1 - y0));
    }

    void draw_background(u32* fb)
    {
        u32 w = gfx::width();
        u32 h = gfx::height();
        u32 sw = stride_words();

        for (u32 y = 0; y < h; y++)
        {
            for (u32 x = 0; x < w; x++)
            {
                u8 r = (u8)((x * 255) / (w ? w : 1));
                u8 g = (u8)((y * 255) / (h ? h : 1));
                u8 b = 0x40;
                fb[y * sw + x] = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
            }
        }

        gfx::mark_dirty(0, 0, w, h);
    }

    void draw_cursor(u32* fb, const input::State& st)
    {
        u32 w = gfx::width();
        u32 h = gfx::height();
        u32 sw = stride_words();

        (void)sw;

        u32 cursor_col = (st.buttons & input::BTN_LEFT) ? 0x00FF0000u : 0x00FFFFFFu;

        s32 cx = st.mouse_x - 4;
        s32 cy = st.mouse_y - 4;

        fill_rect(fb, stride_words(), w, h, cx, cy, 9, 9, cursor_col);
    }

    static constexpr u16 KEY_BACKSPACE = 14;
    static constexpr u16 KEY_ENTER     = 28;
    static constexpr u16 KEY_SPACE     = 57;
    static constexpr u16 KEY_LSHIFT    = 42;
    static constexpr u16 KEY_RSHIFT    = 54;

    static char keycode_to_ascii(u16 code, bool shift)
    {
        // a-z
        if (code >= 30 && code <= 38) // a-i
        {
            char c = (char)('a' + (code - 30));
            return shift ? (char)(c - 'a' + 'A') : c;
        }
        if (code >= 44 && code <= 50) // z-m
        {
            char c = (char)('z' + (code - 44)); // WRONG range, fix below
            (void)c;
        }

        // Full map (minimal but practical)
        switch (code)
        {
            // row: z x c v b n m
            case 44: return shift ? 'Z' : 'z';
            case 45: return shift ? 'X' : 'x';
            case 46: return shift ? 'C' : 'c';
            case 47: return shift ? 'V' : 'v';
            case 48: return shift ? 'B' : 'b';
            case 49: return shift ? 'N' : 'n';
            case 50: return shift ? 'M' : 'm';

            // qwerty row
            case 16: return shift ? 'Q' : 'q';
            case 17: return shift ? 'W' : 'w';
            case 18: return shift ? 'E' : 'e';
            case 19: return shift ? 'R' : 'r';
            case 20: return shift ? 'T' : 't';
            case 21: return shift ? 'Y' : 'y';
            case 22: return shift ? 'U' : 'u';
            case 23: return shift ? 'I' : 'i';
            case 24: return shift ? 'O' : 'o';
            case 25: return shift ? 'P' : 'p';

            // asdf row
            case 30: return shift ? 'A' : 'a';
            case 31: return shift ? 'S' : 's';
            case 32: return shift ? 'D' : 'd';
            case 33: return shift ? 'F' : 'f';
            case 34: return shift ? 'G' : 'g';
            case 35: return shift ? 'H' : 'h';
            case 36: return shift ? 'J' : 'j';
            case 37: return shift ? 'K' : 'k';
            case 38: return shift ? 'L' : 'l';

            // numbers
            case 2:  return shift ? '!' : '1';
            case 3:  return shift ? '@' : '2';
            case 4:  return shift ? '#' : '3';
            case 5:  return shift ? '$' : '4';
            case 6:  return shift ? '%' : '5';
            case 7:  return shift ? '^' : '6';
            case 8:  return shift ? '&' : '7';
            case 9:  return shift ? '*' : '8';
            case 10: return shift ? '(' : '9';
            case 11: return shift ? ')' : '0';

            // punctuation
            case 12: return shift ? '_' : '-';
            case 13: return shift ? '+' : '=';
            case 26: return shift ? '{' : '[';
            case 27: return shift ? '}' : ']';
            case 39: return shift ? ':' : ';';
            case 40: return shift ? '"' : '\'';
            case 41: return shift ? '~' : '`';
            case 43: return shift ? '|' : '\\';
            case 51: return shift ? '<' : ',';
            case 52: return shift ? '>' : '.';
            case 53: return shift ? '?' : '/';

            case KEY_SPACE: return ' ';
            default: return 0;
        }
    }

}

namespace tests
{
    void print_test()
    {
        kprint::puts("print_test: kprint/hex/assert/panic\n");

        kprint::puts("hex: ");
        kprint::hex_u64(0x1234ABCDEF);
        kprint::puts("\n");

        kprint::puts("assert (should pass)\n");
        ASSERT(2 + 2 == 4);

        kprint::puts("print_test done\n");
    }

    void tty_test()
    {
        kprint::puts("tty_test: readline (backspace + enter)\n");
        kprint::puts("type 'quit' to exit this test\n");

        char buf[128];

        while (true)
        {
            kprint::puts("> ");
            usize n = tty::readline(buf, sizeof(buf));

            kprint::puts("line='");
            kprint::puts(buf);
            kprint::puts("' len=");
            kprint::dec_u64(n);
            kprint::puts("\n");

            if (n == 4 && buf[0] == 'q' && buf[1] == 'u' && buf[2] == 'i' && buf[3] == 't')
            {
                break;
            }
        }

        kprint::puts("tty_test done\n");
    }

    void fault_test()
    {
        kprint::puts("fault_test: intentional abort\n");
        kprint::puts("writing to 0xDEADBEEF (should trigger exception handler)\n");

        volatile u64* p = (u64*)(uintptr_t)0xDEADBEEF;
        *p = 1;

        kprint::puts("if you see this, fault did not happen\n");
    }

    void el_test()
    {
        u64 v;
        asm volatile("mrs %0, CurrentEL" : "=r"(v));
        u64 el = (v >> 2) & 0x3;

        kprint::puts("el_test: CurrentEL=");
        kprint::dec_u64(el);
        kprint::puts("\n");
    }

    void timer_test()
    {
        kprint::puts("timer_test: should print dots periodically\n");

        irq::disable();

        gicv2::init();
        gicv2::enable_int(30);

        arch_timer::init_100hz();

        irq::enable();

        kprint::puts("dots: ");
        idle();
    }

    void phys_alloc_test()
    {
        kprint::puts("phys_alloc_test: init + alloc/free\n");

        phys::init();

        void* a = phys::alloc_page();
        void* b = phys::alloc_page();
        void* c = phys::alloc_page();

        kprint::puts("a=");
        kprint::hex_u64((u64)(uintptr_t)a);
        kprint::puts(" b=");
        kprint::hex_u64((u64)(uintptr_t)b);
        kprint::puts(" c=");
        kprint::hex_u64((u64)(uintptr_t)c);
        kprint::puts("\nfree pages=");
        kprint::dec_u64(phys::free_pages());
        kprint::puts("\n");

        phys::free_page(b);

        kprint::puts("freed b, free pages=");
        kprint::dec_u64(phys::free_pages());
        kprint::puts("\n");

        void* d = phys::alloc_page();
        kprint::puts("d=");
        kprint::hex_u64((u64)(uintptr_t)d);
        kprint::puts("\n");

        kprint::puts("phys_alloc_test done\n");
    }

    void mmu_test()
    {
        kprint::puts("mmu_test: enabling MMU\n");

        kprint::puts("before mmu enabled=");
        kprint::dec_u64(mmu::enabled() ? 1 : 0);
        kprint::puts("\n");

        mmu::init();

        kprint::puts("after  mmu enabled=");
        kprint::dec_u64(mmu::enabled() ? 1 : 0);
        kprint::puts("\n");

        kprint::puts("mmu_test done (UART survived)\n");
    }

    void heap_test()
    {
        kprint::puts("heap_test\n");

        kheap::init();
        kheap::stats();

        void* a = kheap::kmalloc(32);
        void* b = kheap::kmalloc(128);
        void* c = kheap::kmalloc(1024);

        kprint::puts("a=");
        kprint::hex_u64((u64)(uintptr_t)a);
        kprint::puts(" b=");
        kprint::hex_u64((u64)(uintptr_t)b);
        kprint::puts(" c=");
        kprint::hex_u64((u64)(uintptr_t)c);
        kprint::puts("\n");

        kheap::stats();

        kheap::kfree(b);
        kheap::kfree(a);

        kprint::puts("freed a,b\n");
        kheap::stats();

        void* d = kheap::kmalloc(64);
        kprint::puts("d=");
        kprint::hex_u64((u64)(uintptr_t)d);
        kprint::puts("\n");

        kheap::stats();

        kheap::kfree(c);
        kheap::kfree(d);

        kprint::puts("heap_test done\n");
        kheap::stats();
    }

    void vm_map_unmap_test()
    {
        kprint::puts("vm_map_unmap_test\n");

        void* p = phys::alloc_page();
        ASSERT(p != nullptr);

        u64 va = 0x60000000ull; 
        u64 pa = (u64)(uintptr_t)p;

        bool ok = vm::map_page(va, pa, vm::READWRITE | vm::NOEXEC);
        ASSERT(ok);

        volatile u64* x = (volatile u64*)(uintptr_t)va;
        *x = 0xCAFEBABE;

        ok = vm::unmap_page(va);
        ASSERT(ok);

        kprint::puts("about to fault (unmapped VA)\n");
        (void)*x; 
    }

    void demand_paging_test()
    {
        kprint::puts("demand_paging_test\n");
        kprint::puts("writing to unmapped VA in demand region (should NOT panic)\n");

        volatile u64* x = (volatile u64*)(uintptr_t)0x60000000ull;

        *x = 0x1122334455667788ull;

        kprint::puts("readback = ");
        kprint::hex_u64(*x);
        kprint::puts("\n");

        kprint::puts("demand_paging_test done\n");
    }

    void virtio_gpu_displayinfo_test(const virtio::PciTransport& t)
    {
        kprint::puts("test: virtio-gpu get_display_info...\n");
        virtio_gpu::DisplayInfo di {};

        if (!virtio_gpu::get_display_info(t, di))
        {
            panic("test: virtio_gpu::get_display_info failed");
        }

        kprint::puts("test: virtio-gpu get_display_info OK\n");
    }

    void virtio_gpu_solid_color_test(const virtio::PciTransport& t)
    {
        kprint::puts("test: virtio-gpu solid color scanout...\n");
        if (!virtio_gpu::solid_color_scanout0_test(t))
        {
            panic("test: virtio-gpu solid color failed");
        }
        kprint::puts("test: virtio-gpu solid color OK\n");
    }

    void virtio_input_poll_test(const virtio::PciTransport& in_t)
    {
        kprint::puts("test: virtio-input init...\n");

        virtio_input::Device dev {};
        if (!dev.init(in_t))
        {
            kprint::puts("test: virtio-input init failed\n");
            return;
        }

        kprint::puts("test: virtio-input polling (press keys / move mouse)...\n");

        while (1)
        {
            virtio_input::Event e {};
            if (dev.poll(e))
            {
                kprint::puts("in: type=");
                kprint::hex_u64(e.type);
                kprint::puts(" code=");
                kprint::hex_u64(e.code);
                kprint::puts(" val=");
                kprint::dec_u64((u64)(u32)e.value);
                kprint::puts("\n");
            }
            else
            {
                asm volatile("yield");
            }
        }
    }

    void virtio_gpu_gfx_gradient_test(const virtio::PciTransport& gpu_t)
    {
        kprint::puts("test: gfx init...\n");
        gfx::init(gpu_t);

        u32* back = gfx::begin_frame();
        draw_background(back);
        gfx::end_frame();

        kprint::puts("test: gradient presented\n");
    }

    void virtio_input_poll_two_test(const virtio::PciTransport& in0, const virtio::PciTransport& in1)
    {
        kprint::puts("test: virtio-input init (two devices)...\n");

        virtio_input::Device d0 {};
        virtio_input::Device d1 {};

        if (!d0.init(in0)) panic("test: virtio-input dev0 init failed");
        if (!d1.init(in1)) panic("test: virtio-input dev1 init failed");

        kprint::puts("test: polling input (serial only)\n");

        while (1)
        {
            virtio_input::Event e {};

            while (d0.poll(e))
            {
                kprint::puts("in0: type=");
                kprint::hex_u64(e.type);
                kprint::puts(" code=");
                kprint::hex_u64(e.code);
                kprint::puts(" val=");
                kprint::dec_u64((u64)e.value);
                kprint::puts("\n");
            }

            while (d1.poll(e))
            {
                kprint::puts("in1: type=");
                kprint::hex_u64(e.type);
                kprint::puts(" code=");
                kprint::hex_u64(e.code);
                kprint::puts(" val=");
                kprint::dec_u64((u64)e.value);
                kprint::puts("\n");
            }

            asm volatile("yield");
        }
    }

    void gui_cursor_test(const virtio::PciTransport& gpu_t,
                         const virtio::PciTransport& in0,
                         const virtio::PciTransport& in1)
    {
        kprint::puts("test: gui_cursor_test begin\n");

        gfx::init(gpu_t);

        virtio_input::Device d0 {};
        virtio_input::Device d1 {};
        if (!d0.init(in0)) panic("gui: input dev0 init failed");
        if (!d1.init(in1)) panic("gui: input dev1 init failed");

        u32 w = gfx::width();
        u32 h = gfx::height();

        input::State st {};
        input::init(st, w, h);

        // draw once immediately
        {
            u32* back = gfx::begin_frame();
            draw_background(back);
            draw_cursor(back, st);
            gfx::end_frame();
        }

        kprint::puts("test: click QEMU window; move mouse -> cursor should move\n");

        u32 frames = 0;

        while (1)
        {
            st.key_changed = false;

            input::poll_device(d0, st, w, h);
            input::poll_device(d1, st, w, h);

            u32* back = gfx::begin_frame();

            draw_background(back);
            draw_cursor(back, st);

            gfx::end_frame();

            frames++;
            if ((frames % 120) == 0)
            {
                kprint::puts("mouse: x=");
                kprint::dec_u64((u64)st.mouse_x);
                kprint::puts(" y=");
                kprint::dec_u64((u64)st.mouse_y);
                kprint::puts(" btn=");
                kprint::hex_u64((u64)st.buttons);
                kprint::puts("\n");
            }

            if (st.key_changed)
            {
                kprint::puts("key: code=");
                kprint::hex_u64(st.last_key_code);
                kprint::puts(" val=");
                kprint::dec_u64((u64)st.last_key_value);
                kprint::puts("\n");
            }

            asm volatile("yield");
        }
    }

    void gui_terminal_test(const virtio::PciTransport& gpu_t,
                       const virtio::PciTransport& in0,
                       const virtio::PciTransport& in1)
{
    gfx::init(gpu_t);

    virtio_input::Device d0 {};
    virtio_input::Device d1 {};
    if (!d0.init(in0)) { panic("term: input dev0 init failed"); }
    if (!d1.init(in1)) { panic("term: input dev1 init failed"); }

    const u32 w = gfx::width();
    const u32 h = gfx::height();

    const u32 stride_words = gfx::stride_bytes() / 4;

    const u32 margin_x = 8;
    const u32 margin_y = 8;

    const u32 usable_w = (w > 2 * margin_x) ? (w - 2 * margin_x) : 0;
    const u32 usable_h = (h > 2 * margin_y) ? (h - 2 * margin_y) : 0;

    const u32 cols = usable_w / 8;
    const u32 rows = usable_h / 8;

    if (cols == 0 || rows == 0)
    {
        panic("term: framebuffer too small for terminal");
    }

    gui_term::Terminal term {};
    gui_term::init(term, cols, rows);
    gui_term::puts(term, "GUI terminal online.\nType here. Backspace + Enter work.\n\n");

    bool shift = false;

    input::State st {};
    input::init(st, w, h);

    while (1)
    {
        st.key_changed = false;
        input::poll_device(d0, st, w, h);
        input::poll_device(d1, st, w, h);

        if (st.key_changed)
        {
            if (st.last_key_code == 42 || st.last_key_code == 54)
            {
                shift = (st.last_key_value != 0);
            }
            else if (st.last_key_value == 1 || st.last_key_value == 2)
            {
                if (st.last_key_code == 14)      { gui_term::putc(term, '\b'); }
                else if (st.last_key_code == 28) { gui_term::putc(term, '\n'); }
                else
                {
                    char c = keycode_to_ascii(st.last_key_code, shift);
                    if (c) { gui_term::putc(term, c); }
                }
            }
        }

        u32* back = gfx::begin_frame();

        for (u32 y = 0; y < h; y++)
        {
            u32* rowp = back + y * stride_words;
            for (u32 x = 0; x < w; x++)
            {
                rowp[x] = 0x00101010u;
            }
        }

        gfx::mark_dirty(0, 0, w, h);

        gui_term::render(term, back, stride_words, margin_x, margin_y, 0x00FFFFFFu, 0x00101010u);

        gfx::end_frame();

        asm volatile("yield");
    }
}


}
