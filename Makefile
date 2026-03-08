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

CXXFLAGS_FP := --target=$(TARGET) \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -O1 -g -Wall -Wextra \
  -nostdlib -nostdinc $(INCLUDES) \
  -mstrict-align \
  -fno-vectorize -fno-slp-vectorize \
  -ffp-contract=off \
  -fno-exceptions -fno-rtti -std=c++20

LDFLAGS := --target=$(TARGET) -fuse-ld=lld -T linker.ld -nostdlib

SRC_DIRS := arch kernel lib

SRCS_S   := $(shell find $(SRC_DIRS) -type f -name '*.S'   | LC_ALL=C sort)
SRCS_CPP := $(shell find $(SRC_DIRS) -type f -name '*.cpp' | LC_ALL=C sort)

OBJS := \
  $(patsubst %.S,  $(OBJDIR)/%.o, $(SRCS_S))  \
  $(patsubst %.cpp,$(OBJDIR)/%.o, $(SRCS_CPP))

all: $(KERNEL)

$(OBJDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/lib/c/%.o: lib/c/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/kernel/apps/calc.o: kernel/apps/calc.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_FP) -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)


run: all
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -m 256 \
	  -kernel $(KERNEL) \
	  -serial mon:stdio \
	  -nographic

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

disk.img:
	python3 -c "import struct,sys; hdr=struct.pack('<IIII',0x5346534F,1,0,17)+b'\x00'*496; sys.stdout.buffer.write(hdr+b'\x00'*(2048*512-512))" > disk.img

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
