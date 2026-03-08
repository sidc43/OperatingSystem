/*
  shellwin.hpp - shell window interface
  open/close/active/tick
*/
#pragma once
#include <stdint.h>

namespace shellwin {

void open();
void close();
bool active();

void tick(uint64_t ticks);

}
