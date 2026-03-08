/*
  fileexplorer.hpp - file explorer interface
  open/close/active/tick
*/
#pragma once
#include <stdint.h>

namespace fileexplorer {

void open();
void close();
bool active();

void tick(uint64_t ticks);

}
