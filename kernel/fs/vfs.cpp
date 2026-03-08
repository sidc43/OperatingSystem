/*
  vfs.cpp - thin virtual filesystem dispatch layer
  all calls currently forward to ramfs
  open/read/write/list_dir/remove - if a real disk backend is added it would plug in here
*/
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/ramfs.hpp"

namespace vfs {

bool open(const char* path, int ) {
    return ramfs::exists(path);
}

int read(const char* path, void* buf, size_t len) {
    return ramfs::read(path, buf, len);
}

int write(const char* path, const void* buf, size_t len) {
    return ramfs::write(path, buf, len);
}

bool exists(const char* path) {
    return ramfs::exists(path);
}

void ls(void (*cb)(const char* name, size_t size, bool is_dir)) {
    ramfs::ls(cb);
}

void ls_in(const char* dir, void (*cb)(const char* name, size_t size, bool is_dir)) {
    ramfs::ls_in(dir, cb);
}

bool mkdir(const char* path) {
    return ramfs::mkdir(path);
}

}
