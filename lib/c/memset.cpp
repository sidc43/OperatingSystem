/*
  memset.cpp - memset implementation
*/
#include <string.h>
#include <stdint.h>

void* memset(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    unsigned char  v = static_cast<unsigned char>(c);

    while (n > 0 && (reinterpret_cast<uintptr_t>(p) & 7u)) {
        *p++ = v;
        --n;
    }

    if (n >= 8) {
        uint64_t w = v;
        w |= w << 8;
        w |= w << 16;
        w |= w << 32;
        uint64_t* q     = reinterpret_cast<uint64_t*>(p);
        size_t    words = n >> 3;
        while (words--) *q++ = w;
        p = reinterpret_cast<unsigned char*>(q);
        n &= 7u;
    }

    while (n--) *p++ = v;

    return s;
}
