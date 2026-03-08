/*
  editor.hpp - text editor interface
  open(path) opens a file, on_key() feeds input, tick() handles cursor blink and redraws
*/
#pragma once
#include <stdint.h>

namespace editor {

bool open(const char* path);

void close();

bool active();

void on_key(char c);

void tick(uint64_t ticks);

}
