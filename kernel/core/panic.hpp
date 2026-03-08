/*
  panic.hpp - halt the kernel with an error message
  panic() is noreturn. KASSERT is a convenience macro that calls it if the condition is false
*/
#pragma once

[[noreturn]] void panic(const char* msg, unsigned long long val = 0);

#define KASSERT(cond) \
  do { if (!(cond)) panic("assertion failed: " #cond); } while(0)
