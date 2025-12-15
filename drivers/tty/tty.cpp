#include "tty.hpp"
#include "drivers/console/console.hpp"
#include "kernel/core/print.hpp"

namespace 
{
    inline bool printable(char c) 
    {
        return c >= 0x20 && c <= 0x7E;
    }

    inline void erase_one() 
    {
        kprint::putc('\b');
        kprint::putc(' ');
        kprint::putc('\b');
    }
}

namespace tty 
{
    usize readline(char* buf, usize max_len) 
    {
        if (max_len == 0) return 0;

        usize len = 0;

        while (true) 
        {
            char c = console::getc();

            if (c == '\r' || c == '\n') 
            {
                kprint::puts("\r\n");
                buf[len] = '\0';
                return len;
            }

            if (c == 0x08 || c == 0x7F) 
            {
                if (len > 0) 
                {
                    len--;
                    erase_one();
                }
                continue;
            }

            if (c == 0x1B) 
            {
                char a = console::getc();
                if (a == '[') 
                {
                    (void)console::getc();
                }
                continue;
            }

            if (printable(c)) 
            {
                if (len + 1 < max_len) 
                {
                    buf[len++] = c;
                    kprint::putc(c);
                } 
                else 
                {
                    kprint::putc('\a');
                }
                continue;
            }
        }
    }
} 
