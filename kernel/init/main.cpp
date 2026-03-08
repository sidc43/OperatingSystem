/*
  main.cpp - this is the kernel entry point
  boots everything in order: uart, mmu, heap, ramfs, gic, timer, virtio
  devices, gpu, wm, keyboard, tablet, then drops into the main event loop
  the event loop polls input, ticks all active apps, and rerenders at ~33hz
*/
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/mmu.hpp"
#include "kernel/irq/gic.hpp"
#include "kernel/irq/timer.hpp"
#include "kernel/drivers/uart_pl011.hpp"
#include "kernel/platform/fdt.hpp"
#include "kernel/drivers/virtio/virtio_mmio.hpp"
#include "kernel/drivers/virtio/gpu.hpp"
#include "kernel/drivers/virtio/input.hpp"
#include "kernel/gfx/draw.hpp"
#include "kernel/gfx/font.hpp"
#include "kernel/wm/wm.hpp"
#include "kernel/fs/ramfs.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/blkfs.hpp"
#include "kernel/drivers/virtio/blk.hpp"
#include "kernel/shell/shell.hpp"
#include "kernel/drivers/virtio/tablet.hpp"
#include "kernel/apps/editor.hpp"
#include "kernel/apps/desktop.hpp"
#include "kernel/apps/controlpanel.hpp"
#include "kernel/apps/shellwin.hpp"
#include "kernel/apps/calc.hpp"
#include "kernel/apps/fileexplorer.hpp"
#include "kernel/apps/sysmon.hpp"
#include "kernel/apps/paint.hpp"
#include "kernel/core/rtc.hpp"
#include "arch/aarch64/regs.hpp"
#include <stdint.h>

static constexpr int      VIRTIO_MAX  = 32;
static constexpr uintptr_t VIRTIO_BASE = 0x0a000000u;
static constexpr uintptr_t VIRTIO_STEP = 0x200u;

static uintptr_t g_virtio[VIRTIO_MAX];
static int       g_nvirtio = 0;

static void probe_virtio_fixed() {
    using namespace virtio;
    g_nvirtio = 0;
    for (int i = 0; i < VIRTIO_MAX; ++i) {
        uintptr_t b = VIRTIO_BASE + (uintptr_t)i * VIRTIO_STEP;
        if (read32(b, MagicValue) == MMIO_MAGIC &&
            read32(b, Version)    == MMIO_VERSION &&
            read32(b, DeviceID)   != 0) {
            g_virtio[g_nvirtio++] = b;
        }
    }
}

static void fmt_clock(char* buf, uint64_t ticks100hz) {
    rtc::DateTime dt = rtc::now(ticks100hz);
    int i = 0;
    buf[i++] = (char)('0' + dt.hour / 10);
    buf[i++] = (char)('0' + dt.hour % 10);
    buf[i++] = ':';
    buf[i++] = (char)('0' + dt.min / 10);
    buf[i++] = (char)('0' + dt.min % 10);
    buf[i++] = ':';
    buf[i++] = (char)('0' + dt.sec / 10);
    buf[i++] = (char)('0' + dt.sec % 10);
    buf[i++] = ' ';
    const char* tz = rtc::tz_name();
    while (*tz) buf[i++] = *tz++;
    buf[i] = '\0';
}

static void print_prompt() {
    const char* cwd = shell::cwd();
    wm::term_puts("root@os:/");
    wm::term_puts(cwd);
    wm::term_puts(" $ ");
}

extern "C" void kernel_main(void* dtb) {

    uart::init();
    print("\n");
    print("=====================================\n");
    print("  AArch64 OS boot\n");
    print("=====================================\n\n");

    mmu::init();
    print("mmu: enabled (caches on, guard page mapped)\n");

    kheap::init();
    printk("heap: %u MiB available\n",
           (unsigned)(kheap::free_bytes() / (1024 * 1024)));

    void* ta = kheap::alloc(64);
    void* tb = kheap::alloc(128, 4096);
    kheap::free(ta);
    kheap::free(tb);
    print("heap: alloc/free self-test passed\n");

    ramfs::init();

    gic::init();

    timer::init(100);

    irq_enable();
    print("irq: enabled\n\n");

    int n = 0;
    if (fdt::valid(dtb)) {
        n = fdt::collect_virtio_mmio_regs(dtb, g_virtio, VIRTIO_MAX);
        printk("fdt: found %d virtio-mmio nodes\n", n);
    }
    if (n == 0) {
        print("fdt: fallback – probing fixed addresses\n");
        probe_virtio_fixed();
        n = g_nvirtio;
    }
    printk("virtio: %d devices found\n", n);

    bool disk_loaded = blkfs::init(g_virtio, n);
    if (!disk_loaded) {

        static const char k_readme[] =
            "AArch64 Bare-Metal OS\n"
            "=====================\n"
            "Files on this disk are persistent via virtio-blk.\n"
            "Use 'sync' to save changes, or they autosave on exit.\n"
            "\n"
            "Commands: ls  cat  touch  rm  mkdir  cd  pwd  echo  clear  exit  help\n";
        static const char k_motd[] =
            "Welcome to AArch64 OS.\n"
            "Type 'help' to list commands.\n";
        ramfs::create("readme.txt", k_readme, sizeof(k_readme) - 1);
        ramfs::create("motd.txt",   k_motd,   sizeof(k_motd)   - 1);
        print("ramfs: seeded with default files\n");
    }

    if (!vgpu::init(g_virtio, n)) {

        print("vgpu: no GPU found – serial-only mode\n");
        print("kernel: entering idle  (tick prints every 5 s)\n\n");
        uint64_t last = 0;
        for (;;) {
            asm volatile("wfi");
            uint64_t t = timer::ticks();
            if (t - last >= 500) { last = t; printk("tick: %u\n", (unsigned)t); }
        }
    }

    wm::init(vgpu::width(), vgpu::height());
    printk("wm: terminal %u × %u chars\n", wm::term_cols(), wm::term_rows());

    desktop::init();
    rtc::init(timer::ticks());

    wm::render();

    kbd::init(g_virtio, n);
    if (kbd::ready()) {
        print("kbd: ready\n");
        wm::render_dirty();
    }

    tablet::init(g_virtio, n, vgpu::width(), vgpu::height());
    if (tablet::ready()) {
        print("tablet: ready\n");
        wm::render_dirty();
    }

    if (blkfs::ready()) {
        print(disk_loaded ? "disk: loaded\n" : "disk: blank\n");
    } else {
        print("disk: none (volatile session)\n");
    }

    char     status_buf[32];
    uint64_t last_render      = 0;
    uint64_t last_tick_update = 0;
    bool     dirty            = false;
    uint32_t line_len         = 0;
    static char line_buf[256];
    int32_t  last_cx = -1, last_cy = -1;

    for (;;) {

        if (tablet::ready()) {
            tablet::poll();
            wm::mouse_update(tablet::cx(), tablet::cy(), tablet::btn_left(), tablet::btn_right());

            int32_t cx = tablet::cx(), cy = tablet::cy();
            if (cx != last_cx || cy != last_cy) {
                last_cx = cx;
                last_cy = cy;
                dirty   = true;
            }
        }

        if (kbd::ready()) {
            bool was_editor = editor::active();
            char c;
            while ((c = kbd::getc_nb()) != 0) {
                if (editor::active()) {

                    editor::on_key(c);
                } else if (calc::active()) {

                    calc::on_key(c);
                } else if (wm::start_menu_wants_keys()) {

                    wm::start_menu_on_key(c);
                } else {

                    if (c == '\r' || c == '\n') {

                        line_buf[line_len] = '\0';
                        print("\n");
                        wm::term_putc('\n');
                        shell::execute(line_buf);
                        line_len = 0;
                        print_prompt();
                    } else if (c == '\b' || c == 127) {

                        if (line_len > 0) {
                            print("\b \b");
                            wm::term_putc('\b');
                            --line_len;
                        }
                    } else if ((uint8_t)c >= 0x20u) {
                        uart::putc(c);
                        wm::term_putc(c);

                        if (line_len < (uint32_t)(sizeof(line_buf) - 1))
                            line_buf[line_len] = c;
                        ++line_len;
                    }
                }
                dirty = true;
            }

            if (was_editor && !editor::active()) {
                line_len = 0;
                print_prompt();
                dirty = true;
            }
        }

        uint64_t t = timer::ticks();

        editor::tick(t);
        if (editor::active()) dirty = true;

        controlpanel::tick(t);

        calc::tick(t);
        if (calc::active()) dirty = true;

        fileexplorer::tick(t);
        if (fileexplorer::active()) dirty = true;

        sysmon::tick(t);
        if (sysmon::active()) dirty = true;

        paint::tick(t);
        if (paint::active()) dirty = true;

        if (wm::desktop_was_clicked()) {
            desktop::on_click(wm::desktop_click_x(), wm::desktop_click_y());

            if (desktop::shell_was_opened()) {
                wm::term_clear();
                wm::term_set_cursor(0, 0);
                print_prompt();
            }
            dirty = true;
        }

        {
            int start_idx = -1;
            if (wm::start_app_was_selected(start_idx)) {
                if (start_idx >= 0 && start_idx <= 6) {

                    desktop::launch_app(start_idx);
                    if (desktop::shell_was_opened()) {
                        wm::term_clear();
                        wm::term_set_cursor(0, 0);
                        print_prompt();
                    }
                } else if (start_idx == 7) {

                    blkfs::flush();
                    register uint64_t x0 asm("x0") = 0x84000008ULL;
                    asm volatile("hvc #0" :: "r"(x0) : "memory");
                    for (;;) asm volatile("wfi");
                }
                dirty = true;
            }
        }

        if (t - last_tick_update >= 100) {
            last_tick_update = t;
            fmt_clock(status_buf, t);
            wm::set_status(status_buf);
            dirty = true;
        }

        shellwin::tick(t);
        if (shellwin::active()) dirty = true;

        if (dirty && (t - last_render >= 3)) {
            last_render = t;
            dirty       = false;
            sysmon::record_frame();
            wm::render_dirty();
        }

        asm volatile("wfi");
    }
}
