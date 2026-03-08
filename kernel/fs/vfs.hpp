/*
  vfs.hpp - virtual filesystem interface
  open/read/write/list_dir/remove/mkdir/exists
  flat path convention for now, no symlinks or permissions
*/
#pragma once
#include <stddef.h>

namespace vfs {

static constexpr int O_RDONLY = 0;
static constexpr int O_WRONLY = 1;
static constexpr int O_RDWR   = 2;
static constexpr int O_CREAT  = 4;

bool   open  (const char* path, int flags);

int    read  (const char* path, void* buf, size_t len);

int    write (const char* path, const void* buf, size_t len);

bool   exists(const char* path);

void   ls    (void (*cb)(const char* name, size_t size, bool is_dir));

void   ls_in (const char* dir,
              void (*cb)(const char* name, size_t size, bool is_dir));

bool   mkdir (const char* path);

}
