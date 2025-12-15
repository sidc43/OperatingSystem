#pragma once
#include <stdint.h>

namespace pl011 
{
    void init(uintptr_t base = 0x09000000);
    void putc(char c);
    char getc();
    void puts(const char* s);
}