/*
  uart_pl011.hpp - pl011 uart interface
  init() sets up 115200 8n1, putc() sends a char, getc() returns -1 if nothing waiting
*/
#pragma once
#include <stdint.h>

namespace uart {

  void init();

  void putc(char c);

  int  getc();
}
