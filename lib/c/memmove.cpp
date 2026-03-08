/*
  memmove.cpp - memmove that handles overlapping regions
*/
#include <string.h>
#include <stdint.h>

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char*       d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);

    if (d == s || n == 0) return dst;

    if (d < s || d >= s + n) {

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
    } else {

        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(a);
    const unsigned char* q = static_cast<const unsigned char*>(b);
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}
