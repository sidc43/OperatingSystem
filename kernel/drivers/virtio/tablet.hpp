/*
  tablet.hpp - virtio-tablet driver interface
  init/poll/cx/cy/btn_left/btn_right/ready
*/
#pragma once
#include <stdint.h>

namespace tablet {

bool init(const uintptr_t* bases, int n_bases,
          uint32_t screen_w, uint32_t screen_h);

void poll();

int32_t cx();
int32_t cy();

bool btn_left();

bool btn_right();

bool ready();

}
