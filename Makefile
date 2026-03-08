TARGET := aarch64-none-elf

CC  := clang
CXX := clang++

BUILD  := build
OBJDIR := $(BUILD)/obj
KERNEL := $(BUILD)/kernel.elf

INCLUDES := -I. -Iinclude

CFLAGS := --target=$(TARGET) \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -O2 -g -Wall -Wextra \
  -nostdlib -nostdinc $(INCLUDES) \
  -mgeneral-regs-only \
  -mstrict-align \
  -fno-vectorize -fno-slp-vectorize

CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -std=c++20

# Variant without -mgeneral-regs-only, used for files that need hardware FP
# (e.g. the calculator uses double arithmetic via scalar FP registers).
# -ffp-contract=off prevents the compiler from emitting fma() calls.
CXXFLAGS_FP := --target=$(TARGET) \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -O1 -g -Wall -Wextra \
  -nostdlib -nostdinc $(INCLUDES) \
  -mstrict-align \
  -fno-vectorize -fno-slp-vectorize \
  -ffp-contract=off \
  -fno-exceptions -fno-rtti -std=c++20

LDFLAGS := --target=$(TARGET) -fuse-ld=lld -T linker.ld -nostdlib

# ── Sources (auto-discovered) ─────────────────────────────────────────────────
SRC_DIRS := arch kernel lib

SRCS_S   := $(shell find $(SRC_DIRS) -type f -name '*.S'   | LC_ALL=C sort)
SRCS_CPP := $(shell find $(SRC_DIRS) -type f -name '*.cpp' | LC_ALL=C sort)

OBJS := \
  $(patsubst %.S,  $(OBJDIR)/%.o, $(SRCS_S))  \
  $(patsubst %.cpp,$(OBJDIR)/%.o, $(SRCS_CPP))

# ── Rules ─────────────────────────────────────────────────────────────────────
all: $(KERNEL)

$(OBJDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# lib/c freestanding C routines: same CXXFLAGS as everything else (no-NEON via
# -mgeneral-regs-only already set globally). Must appear BEFORE generic %.cpp.
$(OBJDIR)/lib/c/%.o: lib/c/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Calculator needs hardware FP (double arithmetic) — compile without
# -mgeneral-regs-only so scalar FP registers are available.
$(OBJDIR)/kernel/apps/calc.o: kernel/apps/calc.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_FP) -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# ── QEMU targets ──────────────────────────────────────────────────────────────

# Serial-only (no display needed for Phase 1 testing)
run: all
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 256 \
	  -kernel $(KERNEL) \
	  -serial mon:stdio \
	  -nographic

# With virtio-gpu / keyboard / mouse for later phases
run-gui: all disk.img
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 1024 \
	  -kernel $(KERNEL) \
	  -serial mon:stdio \
	  -display cocoa,full-grab=on \
	  -global virtio-mmio.force-legacy=false \
	  -device virtio-gpu-device \
	  -device virtio-keyboard-device \
	  -device virtio-tablet-device \
	  -drive file=disk.img,if=none,format=raw,id=hd0 \
	  -device virtio-blk-device,drive=hd0

# Debug GUI: serial goes to /tmp/kbd.log, monitor on stdio.
# Open a second terminal and run: tail -f /tmp/kbd.log
# Then sendkey e here; characters appear in the log if the kernel receives them.
run-gui-debug: all
	@rm -f /tmp/kbd.log
	@echo "Serial log: /tmp/kbd.log  (run 'tail -f /tmp/kbd.log' in another terminal)"
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 1024 \
	  -kernel $(KERNEL) \
	  -serial file:/tmp/kbd.log \
	  -monitor stdio \
	  -display cocoa,full-grab=on \
	  -global virtio-mmio.force-legacy=false \
	  -device virtio-gpu-device \
	  -device virtio-keyboard-device \
	  -device virtio-tablet-device

# VNC display: keyboard always works via VNC protocol (no grab issues).
# macOS Screen Sharing requires a password even for no-auth VNC.
# Easiest client:  brew install tiger-vnc  →  vncviewer localhost:5900
# Or use the macOS built-in but type any password (e.g. just hit Return)
# to bypass the prompt when QEMU has no auth set.
run-vnc: all
	@echo "------------------------------------------------------"
	@echo " VNC server: localhost:5900  (no password)"
	@echo " Connect with:  vncviewer localhost:5900"
	@echo " (install:  brew install tiger-vnc)"
	@echo " OR macOS Screen Sharing – just press Return at password prompt"
	@echo "------------------------------------------------------"
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 1024 \
	  -kernel $(KERNEL) \
	  -serial mon:stdio \
	  -display vnc=127.0.0.1:0 \
	  -global virtio-mmio.force-legacy=false \
	  -device virtio-gpu-device \
	  -device virtio-keyboard-device \
	  -device virtio-tablet-device

# Headless keyboard debug: serial only, inject fake keypresses via monitor.
# Usage: make run-kbd-test
# In the QEMU monitor (Ctrl+A C to enter): sendkey a  sendkey b  sendkey ret
run-kbd-test: all
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 1024 \
	  -kernel $(KERNEL) \
	  -serial mon:stdio \
	  -display none \
	  -global virtio-mmio.force-legacy=false \
	  -device virtio-gpu-device \
	  -device virtio-keyboard-device

# ── Disk image ───────────────────────────────────────────────────────────────
# Creates a blank 1 MiB OSFS-formatted disk image (MAGIC=0x5346534F, 2048 sectors).
# Only built once; use 'make clean' to remove and regenerate.
disk.img:
	python3 -c "import struct,sys; hdr=struct.pack('<IIII',0x5346534F,1,0,17)+b'\x00'*496; sys.stdout.buffer.write(hdr+b'\x00'*(2048*512-512))" > disk.img

# ── Misc ──────────────────────────────────────────────────────────────────────

# Regenerate icon C arrays from PNG sources in assets/.
# Requires Pillow:  pip3 install Pillow
generate-icons:
	@mkdir -p kernel/gfx/assets
	python3 tools/img2c.py assets/shell.png        icon_shell        48 48 > kernel/gfx/assets/icon_shell.hpp
	python3 tools/img2c.py assets/editor.png       icon_editor       48 48 > kernel/gfx/assets/icon_editor.hpp
	python3 tools/img2c.py assets/controlpanel.png icon_controlpanel 48 48 > kernel/gfx/assets/icon_controlpanel.hpp
	@echo "Icons regenerated in kernel/gfx/assets/"

clean:
	rm -rf $(BUILD) disk.img

disasm: $(KERNEL)
	llvm-objdump -d --no-show-raw-insn $(KERNEL) | less

.PHONY: all run run-gui run-gui-debug run-vnc run-kbd-test generate-icons clean disasm
