/*
  ramfs.hpp - ram filesystem interface
  init/create/read/write/list/remove/mkdir/exists
  entries are heap-allocated, max 64 files, name up to 64 chars
*/
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ramfs {

static constexpr size_t MAX_FILES = 64;
static constexpr size_t NAME_MAX  = 64;

struct Entry {
    char     name[NAME_MAX];
    uint8_t* data;
    size_t   size;
    bool     used;
    bool     is_dir;
};

void  init();

bool  create(const char* name, const void* data, size_t size);

int   read  (const char* name, void* buf, size_t len);

int   write (const char* name, const void* buf, size_t len);

bool  exists(const char* name);

void  ls    (void (*cb)(const char* name, size_t size, bool is_dir));

void  ls_in (const char* dir,
             void (*cb)(const char* name, size_t size, bool is_dir));

bool  remove(const char* name);

bool  mkdir (const char* name);

bool  exists_dir(const char* name);

const Entry* table();

}
