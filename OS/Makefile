SRC_DIR = src
INCLUDE_DIR = include
BIN_DIR = bin

CXX = g++
NASM = nasm
LD = ld
CXXFLAGS = -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -no-pie -m32 -I$(INCLUDE_DIR)
NASMFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T $(SRC_DIR)/link.ld -nostdlib

ASM_SOURCES = $(SRC_DIR)/kernel.asm
CPP_SOURCES = $(SRC_DIR)/kernel.cpp

ASM_OBJECTS = $(BIN_DIR)/kernel.o
CPP_OBJECTS = $(BIN_DIR)/kernel_cpp.o
OBJECTS = $(ASM_OBJECTS) $(CPP_OBJECTS)

KERNEL_BIN = $(BIN_DIR)/kernel

all: $(KERNEL_BIN)

$(ASM_OBJECTS): $(ASM_SOURCES) | $(BIN_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(CPP_OBJECTS): $(CPP_SOURCES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJECTS) $(SRC_DIR)/link.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

run: $(KERNEL_BIN)
	qemu-system-i386 -kernel $(KERNEL_BIN) -no-reboot -boot order=d 

.PHONY: all clean run
