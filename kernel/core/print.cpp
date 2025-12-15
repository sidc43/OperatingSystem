#include "kernel/core/print.hpp"
#include "drivers/console/console.hpp"

namespace kprint 
{
    void putc(char c) { console::putc(c); }
    void puts(const char* s) { console::puts(s); }

    void hex_u64(u64 x) 
    {
        static const char* H = "0123456789ABCDEF";
        puts("0x");
        for (int i = 60; i >= 0; i -= 4) 
        {
            putc(H[(x >> i) & 0xF]);
        }
    }

    void dec_u64(u64 x) 
    {
        char buf[32];
        int i = 0;

        if (x == 0) 
        {
            putc('0');
            return;
        }

        while (x > 0 && i < (int)sizeof(buf)) 
        {
            buf[i++] = char('0' + (x % 10));
            x /= 10;
        }

        while (i--) putc(buf[i]);
    }
}
