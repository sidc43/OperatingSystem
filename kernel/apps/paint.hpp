/*
  paint.hpp - paint app interface
  open/close/active/tick
*/
#pragma once
#include <stdint.h>

namespace paint {

void open();

void close();

bool active();

void tick(uint64_t ticks);

}
