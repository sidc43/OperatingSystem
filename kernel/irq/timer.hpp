/*
  timer.hpp - arm generic timer interface
  init(hz) sets up the tick rate, ticks() returns elapsed count, sleep_ms() busy-waits
*/
#pragma once
#include <stdint.h>

namespace timer {

  void init(uint32_t hz);

  uint64_t ticks();

  void sleep_ms(uint32_t ms);
}
