/*
  sysmon.hpp - system monitor interface
  open/close/active/tick/record_frame
  record_frame() is called from the main render loop to track fps
*/
#pragma once
#include <stdint.h>

namespace sysmon {

void open();

void close();

bool active();

void record_frame();

void tick(uint64_t ticks_100hz);

}
