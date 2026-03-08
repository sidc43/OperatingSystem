/*
  desktop.hpp - desktop icon layer interface
  init() sets up the grid, render() draws icons, on_click() handles mouse events
  launch_app(idx) opens apps by start-menu index
*/
#pragma once
#include <stdint.h>

namespace desktop {

void init();

void render();

void on_click(int32_t x, int32_t y);

bool shell_was_opened();

void launch_app(int i);

uint32_t icon_zone_term_rows();

}
