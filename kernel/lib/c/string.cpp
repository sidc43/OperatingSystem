#include "kernel/lib/c/string.hpp"

int kstrcmp(const char* a, const char* b) 
{
    while (*a && (*a == *b)) 
    {
        ++a;
        ++b;
    }
    return (unsigned char)(*a) - (unsigned char)(*b);
}
