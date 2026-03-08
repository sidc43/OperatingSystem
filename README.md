# aarch64 os

a bare-metal operating system for aarch64 that runs on qemu. it has a full gui with a window manager, taskbar, start menu, and a bunch of apps. everything runs without linux - just the kernel, drivers written from scratch, and a graphics stack on top of virtio-gpu.

## what's in it

**system**
- aarch64 kernel booting directly via qemu virt machine
- identity-mapped mmu with data/instruction caches on
- bump-pointer heap allocator
- arm generic timer at 100hz, gicv2 interrupt controller
- pl011 uart for serial debug output

**storage**
- virtio-blk driver for persistent disk access
- simple flat filesystem (osfs) that saves to a disk image
- in-memory ramfs that syncs to disk on shutdown

**graphics + input**
- virtio-gpu framebuffer driver (bgra 32bpp)
- virtio-keyboard driver with full ascii + nav key decoding
- virtio-tablet driver for absolute mouse position
- software rasterizer: fill_rect, draw_text, blit, alpha composite, lines

**window manager**
- floating windows with drag, title bar, close/maximize buttons
- taskbar with window buttons
- start menu with pinned apps + scrollable/searchable "all programs" panel
- desktop with icon grid (single-click select, double-click launch)
- win98-style ui theme

**apps**
- **shell** - terminal emulator with a small unix command set (ls, cat, touch, rm, mkdir, cd, echo, clear)
- **text editor** - line-based editor with arrow keys, home/end, pgup/pgdn, save/quit
- **calculator** - 4-function floating-point with keyboard + mouse input
- **file explorer** - scrollable file browser with right-click context menu, open/delete/new
- **paint** - pixel-art canvas with palette swatches and three brush sizes
- **system monitor** - cpu chart, memory bar, uptime, process list with end-task
- **control panel** - clock, wallpaper color picker, per-app window size settings, shutdown

**clock**
- reads the pl031 hardware rtc (qemu exposes actual host time)
- falls back to compile-time `__DATE__`/`__TIME__` if rtc reads garbage
- configurable timezone from control panel

## how to run

you need qemu and an aarch64 cross-compiler (clang with llvm-lld works great)

**install deps on macos**
```
brew install qemu llvm
```

**build and run**
```
make run-gui
```

that builds the kernel elf, creates a blank disk image, and launches qemu with the gpu, keyboard, and tablet devices. the gui comes up in a cocoa window.

**serial only (no gui)**
```
make run
```

boots in nographic mode, shell runs in the terminal

**to quit**
use shut down in the start menu (it calls psci power-off smc), or press `ctrl-a x` in the qemu window

