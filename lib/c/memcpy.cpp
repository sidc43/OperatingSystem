/*
  memcpy.cpp - byte-by-byte memcpy implementation
*/
#include <string.h>
#include <stdint.h>

void* memcpy(void* __restrict dest, const void* __restrict src, size_t n) {
    unsigned char*       d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);

    while (n > 0 && (reinterpret_cast<uintptr_t>(d) & 7u)) {
        *d++ = *s++;
        --n;
    }

    if (n >= 8 && !(reinterpret_cast<uintptr_t>(s) & 7u)) {
        const uint64_t* qs    = reinterpret_cast<const uint64_t*>(s);
        uint64_t*       qd    = reinterpret_cast<uint64_t*>(d);
        size_t          words = n >> 3;
        while (words--) *qd++ = *qs++;
        d = reinterpret_cast<unsigned char*>(qd);
        s = reinterpret_cast<const unsigned char*>(qs);
        n &= 7u;
    }

    while (n--) *d++ = *s++;

    return dest;
}
