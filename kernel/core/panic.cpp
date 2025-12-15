#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"

[[noreturn]] void panic(const char* msg) 
{
    kprint::puts("\n\n=== PANIC ===\n");
    kprint::puts(msg);
    kprint::puts("\n");

    while (1) { asm volatile("wfe"); }
}
