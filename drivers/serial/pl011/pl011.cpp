#include "pl011.hpp"
#include "hal/mmio.hpp"

namespace 
{
    uintptr_t g_base = 0x09000000;

    constexpr uintptr_t DR = 0x00;
    constexpr uintptr_t FR = 0x18;

    constexpr uint32_t FR_TXFF = (1u << 5);
    constexpr uint32_t FR_RXFE = (1u << 4);
}

namespace pl011 
{
    void init(uintptr_t base) 
    {
        g_base = base;
    }

    void putc(char c) 
    {
        while (mmio_read32(g_base + FR) & FR_TXFF) {}
        mmio_write32(g_base + DR, (uint32_t)c);
    }

    char getc() 
    {
        while (mmio_read32(g_base + FR) & FR_RXFE) {}
        return (char)(mmio_read32(g_base + DR) & 0xFF);
    }

    void puts(const char* s) 
    {
        while (*s) putc(*s++);
    }
} 
