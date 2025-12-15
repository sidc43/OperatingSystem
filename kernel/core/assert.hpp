#pragma once
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#define ASSERT(expr) do { \
    if (!(expr)) { \
        kprint::puts("\n\n=== ASSERT FAILED ===\n"); \
        kprint::puts("expr: " #expr "\n"); \
        kprint::puts("file: " __FILE__ "\n"); \
        kprint::puts("line: "); \
        kprint::dec_u64((unsigned long long)__LINE__); \
        kprint::puts("\n"); \
        panic("assert"); \
    } \
} while (0)
