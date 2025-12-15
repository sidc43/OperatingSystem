#include "types.hpp"

extern "C" void* memset(void* dst, int c, usize n)
{
    u8* p = (u8*)dst;
    u8 v = (u8)c;

    for (usize i = 0; i < n; i++)
    {
        p[i] = v;
    }

    return dst;
}

extern "C" void* memcpy(void* dst, const void* src, usize n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    for (usize i = 0; i < n; i++)
    {
        d[i] = s[i];
    }

    return dst;
}

extern "C" void* memmove(void* dst, const void* src, usize n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;

    if (d == s || n == 0)
    {
        return dst;
    }

    if (d < s)
    {
        for (usize i = 0; i < n; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        for (usize i = n; i > 0; i--)
        {
            d[i - 1] = s[i - 1];
        }
    }

    return dst;
}

extern "C" int memcmp(const void* a, const void* b, usize n)
{
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;

    for (usize i = 0; i < n; i++)
    {
        if (x[i] != y[i])
        {
            return (int)x[i] - (int)y[i];
        }
    }

    return 0;
}
