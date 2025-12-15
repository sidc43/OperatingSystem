ARCH := aarch64
BUILD := build

CC  := aarch64-elf-gcc
CXX := aarch64-elf-g++
LD  := aarch64-elf-ld
QEMU_MEM := 512M

KERNEL_ELF := $(BUILD)/kernel.elf

INCLUDES := -Iinclude -I. -Ikernel -Idrivers -Ihal -Isrc

CFLAGS   := -ffreestanding -nostdlib -fno-builtin -O2 -Wall -Wextra -MMD -MP -mgeneral-regs-only
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  := $(CFLAGS)

LDFLAGS  := -T linker.ld -nostdlib

ASM_SOURCES := \
  src/arch/aarch64/boot/boot.S \
  src/arch/aarch64/boot/vectors.S \
  src/arch/aarch64/cpu/context_switch.S \
  src/arch/aarch64/cpu/thread_trampoline.S \
  src/arch/aarch64/cpu/enter_el0.S \
  src/arch/aarch64/usermode/el0_blob.S


# Compile everything, but exclude the kernel-side exceptions folder to avoid duplicate exception_dispatch.
CPP_SOURCES := $(filter-out kernel/arch/aarch64/exceptions/%,$(shell find kernel drivers hal src -name '*.cpp' 2>/dev/null))

ASM_OBJS := $(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES))
CPP_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(CPP_SOURCES))
OBJS := $(ASM_OBJS) $(CPP_OBJS)

DEPS := $(OBJS:.o=.d)

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $(INCLUDES) -c $< -o $@

run: $(KERNEL_ELF)
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m $(QEMU_MEM) \
		-nographic \
		-kernel $(KERNEL_ELF) \
		-serial mon:stdio

clean:
	rm -rf $(BUILD)

-include $(DEPS)
