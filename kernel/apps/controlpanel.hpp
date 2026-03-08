/*
  controlpanel.hpp - control panel interface
  open/close/active/tick
  also exposes app_pref_w()/app_pref_h() so other apps can read size preferences
*/
#pragma once
#include <stdint.h>

namespace controlpanel {

void open();

void close();

bool active();

void tick(uint64_t timer_ticks_100hz);

uint32_t app_pref_w(int idx);
uint32_t app_pref_h(int idx);

}
