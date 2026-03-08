/*
  print.cpp - serial debug output to the pl011 uart
  putc, print, print_hex, print_dec, and a minimal printk (%s %c %d %u %x %p)
  no heap, no dependencies, always available even before full boot
*/
#include "kernel/core/print.hpp"
#include "kernel/drivers/uart_pl011.hpp"
#include <stdarg.h>

void putc(char c) {
    uart::putc(c);
}

void print(const char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') uart::putc('\r');
        uart::putc(*s++);
    }
}

void print_hex(unsigned long long v) {
    uart::putc('0'); uart::putc('x');
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (v >> i) & 0xF;
        uart::putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
}

void print_dec(unsigned long long v) {
    if (v == 0) { uart::putc('0'); return; }
    char buf[21];
    int  i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) uart::putc(buf[i]);
}

void printk(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { putc(*fmt++); continue; }
        fmt++;
        switch (*fmt++) {
            case 's': { const char* s = va_arg(ap, const char*); print(s ? s : "(null)"); break; }
            case 'c': { putc((char)va_arg(ap, int)); break; }
            case 'd': { long long v = va_arg(ap, long long);
                        if (v < 0) { putc('-'); v = -v; } print_dec((unsigned long long)v); break; }
            case 'u': { print_dec(va_arg(ap, unsigned long long)); break; }
            case 'x': { print_hex(va_arg(ap, unsigned long long)); break; }
            case 'p': { print_hex((unsigned long long)(uintptr_t)va_arg(ap, void*)); break; }
            case '%': { putc('%'); break; }
            default:  { putc('?'); break; }
        }
    }
    va_end(ap);
}
