#pragma once
#include <stdint.h>

static inline void mmio_write32(uintptr_t addr, uint32_t value) 
{
    *(volatile uint32_t*)addr = value;
}

static inline uint32_t mmio_read32(uintptr_t addr) 
{
    return *(volatile uint32_t*)addr;
}