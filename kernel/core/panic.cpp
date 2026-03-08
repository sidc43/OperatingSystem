/*
  panic.cpp - drops into a permanent halt with an error message
  masks all interrupts so nothing can interrupt the death print
  also used by KASSERT. once called there is no recovery
*/
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"

[[noreturn]] void panic(const char* msg, unsigned long long val) {

    asm volatile("msr daifset, #0xF" ::: "memory");

    print("\n\n*** KERNEL PANIC ***\n");
    print("    ");
    print(msg);
    if (val) {
        print("  (");
        print_hex(val);
        print(")");
    }
    print("\n");

    for (;;) asm volatile("wfe");
}
