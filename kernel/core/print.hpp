/*
  print.hpp - declarations for serial debug output helpers
  printk supports %s %c %d %u %x %p %%
*/
#pragma once

void putc(char c);

void print(const char* s);

void print_hex(unsigned long long v);
void print_dec(unsigned long long v);

void printk(const char* fmt, ...);
