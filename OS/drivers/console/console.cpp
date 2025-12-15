#include "console.hpp"
#include "drivers/serial/pl011/pl011.hpp"

namespace console 
{
    void init() 
    {
        pl011::init(0x09000000); 
    }

    void putc(char c) { pl011::putc(c); }
    void puts(const char* s) { pl011::puts(s); }
    char getc() { return pl011::getc(); }
}