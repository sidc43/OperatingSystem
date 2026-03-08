/*
  shell.hpp - command interpreter interface
  execute(line) runs one command. cwd() returns the current directory path
*/
#pragma once

namespace shell {

void execute(const char* line);

const char* cwd();

}
