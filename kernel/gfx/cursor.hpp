/*
  cursor.hpp - hardware cursor sprite interface
  set_pos(), save_bg(), restore_bg(), draw() - use in that order each frame
  dirty_rect() gives you the bounding box to flush after a cursor-only update
*/
#pragma once
#include <stdint.h>

namespace cursor {

void    set_pos(int32_t x, int32_t y);
int32_t pos_x();
int32_t pos_y();

void save_bg();

void restore_bg();

bool dirty_rect(int32_t& x, int32_t& y, uint32_t& w, uint32_t& h);

void draw();

}
