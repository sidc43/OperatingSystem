/*
  string.h - freestanding string/memory function declarations with c linkage
  implementations are in lib/c/
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void*  memcpy (void* __restrict dst, const void* __restrict src, size_t n);
void*  memmove(void* dst, const void* src, size_t n);
void*  memset (void* s, int c, size_t n);
int    memcmp (const void* a, const void* b, size_t n);
size_t strlen (const char* s);
size_t strnlen(const char* s, size_t max);
int    strcmp (const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);

#ifdef __cplusplus
}
#endif
