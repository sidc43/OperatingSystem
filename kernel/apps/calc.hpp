/*
  calc.hpp - calculator app interface
  open/close/active/on_key/tick - tick handles mouse clicks and redraws
*/
#pragma once
#include <stdint.h>

namespace calc {

void open();

void close();

bool active();

void on_key(char c);

void tick(uint64_t ticks);

}
