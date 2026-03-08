/*
  gpu.hpp - virtio-gpu driver interface
  init(), framebuffer(), width(), height(), flush_rect(), flush_full()
  pixel format is bgra b8g8r8x8
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace vgpu {

bool init(const uintptr_t* bases, int n_bases);

uint32_t* framebuffer();

uint32_t width();
uint32_t height();

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

void flush_full();

void flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

bool ready();

}
