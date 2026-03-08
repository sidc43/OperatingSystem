/*
  input.hpp - virtio-input keyboard driver interface
  init/poll/getc_nb/getc/ready
  also exposes special key byte constants (arrow keys, home/end etc)
*/
#pragma once
#include <stdint.h>

namespace kbd {

bool init(const uintptr_t* bases, int n_bases);

void poll();

char getc_nb();

char getc();

bool ready();

}
