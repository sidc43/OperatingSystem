/*
  gic.hpp - gicv2 driver interface
  init, enable_irq, disable_irq, set_priority, register_handler
  dispatch() is called by the irq vector in vectors.S
*/
#pragma once
#include <stdint.h>

namespace gic {

  void init();

  void enable_irq(uint32_t irq);

  void disable_irq(uint32_t irq);

  void set_priority(uint32_t irq, uint8_t prio);

  using Handler = void (*)();
  void register_handler(uint32_t irq, Handler h);

  extern "C" void dispatch();
}
