# aarch64 os

a bare-metal operating system for aarch64 that runs on qemu. it has a full gui with a window manager, taskbar, start menu, and some apps.

<img width="1288" height="842" alt="image" src="https://github.com/user-attachments/assets/0e54cdd8-3855-489c-be82-00f2fde49fe3" />
<img width="1288" height="842" alt="image" src="https://github.com/user-attachments/assets/1ca8f4b2-c35c-4403-94fe-eaf7a9c1e2df" />

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

