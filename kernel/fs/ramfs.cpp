/*
  ramfs.cpp - flat in-memory filesystem backed by kheap
  supports up to 64 files/dirs, each with a short name and heap-allocated data
  used as the primary fs on boot. blkfs syncs to/from disk on top of this
*/
#include "kernel/fs/ramfs.hpp"
#include "kernel/mm/heap.hpp"
#include <string.h>
#include <stdint.h>

namespace {

static ramfs::Entry g_table[ramfs::MAX_FILES];

static int find_name(const char* name) {
    for (size_t i = 0; i < ramfs::MAX_FILES; ++i)
        if (g_table[i].used && strcmp(g_table[i].name, name) == 0)
            return (int)i;
    return -1;
}

static int find_free() {
    for (size_t i = 0; i < ramfs::MAX_FILES; ++i)
        if (!g_table[i].used) return (int)i;
    return -1;
}

static void name_copy(char* dst, const char* src) {
    size_t i = 0;
    for (; i < ramfs::NAME_MAX - 1 && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

}

namespace ramfs {

void init() {
    memset(g_table, 0, sizeof(g_table));
}

bool create(const char* name, const void* data, size_t size) {
    if (!name || name[0] == '\0') return false;

    int slot = find_name(name);
    if (slot >= 0) {
        if (g_table[slot].is_dir) return false;

        kheap::free(g_table[slot].data);
        g_table[slot].data = nullptr;
        g_table[slot].size = 0;
    } else {
        slot = find_free();
        if (slot < 0) return false;
        name_copy(g_table[slot].name, name);
        g_table[slot].used   = true;
        g_table[slot].is_dir = false;
        g_table[slot].data   = nullptr;
        g_table[slot].size   = 0;
    }

    if (size > 0 && data) {
        uint8_t* buf = (uint8_t*)kheap::alloc(size, 1);
        if (!buf) {

            memset(&g_table[slot], 0, sizeof(Entry));
            return false;
        }
        memcpy(buf, data, size);
        g_table[slot].data = buf;
        g_table[slot].size = size;
    }

    return true;
}

int read(const char* name, void* buf, size_t len) {
    if (!name || !buf) return -1;
    int slot = find_name(name);
    if (slot < 0) return -1;

    size_t n = (g_table[slot].size < len) ? g_table[slot].size : len;
    if (n > 0 && g_table[slot].data)
        memcpy(buf, g_table[slot].data, n);
    return (int)n;
}

int write(const char* name, const void* buf, size_t len) {
    if (!name || (!buf && len > 0)) return -1;
    return create(name, buf, len) ? (int)len : -1;
}

bool exists(const char* name) {
    if (!name) return false;
    return find_name(name) >= 0;
}

void ls(void (*cb)(const char* name, size_t size, bool is_dir)) {
    if (!cb) return;
    for (size_t i = 0; i < MAX_FILES; ++i)
        if (g_table[i].used)
            cb(g_table[i].name, g_table[i].size, g_table[i].is_dir);
}

void ls_in(const char* dir, void (*cb)(const char* name, size_t size, bool is_dir)) {
    if (!cb) return;
    size_t dir_len = (dir && dir[0]) ? strlen(dir) : 0u;
    for (size_t i = 0; i < MAX_FILES; ++i) {
        if (!g_table[i].used) continue;
        const char* fullname = g_table[i].name;
        const char* child;
        if (dir_len == 0) {

            child = fullname;
        } else {

            if (strncmp(fullname, dir, dir_len) != 0) continue;
            if (fullname[dir_len] != '/') continue;
            child = fullname + dir_len + 1u;
        }

        if (!child[0]) continue;
        bool has_slash = false;
        for (size_t j = 0; child[j]; ++j)
            if (child[j] == '/') { has_slash = true; break; }
        if (has_slash) continue;
        cb(child, g_table[i].size, g_table[i].is_dir);
    }
}

bool remove(const char* name) {
    if (!name) return false;
    int slot = find_name(name);
    if (slot < 0) return false;

    kheap::free(g_table[slot].data);
    memset(&g_table[slot], 0, sizeof(Entry));
    return true;
}

bool mkdir(const char* name) {
    if (!name || name[0] == '\0') return false;
    if (find_name(name) >= 0) return false;
    int slot = find_free();
    if (slot < 0) return false;
    name_copy(g_table[slot].name, name);
    g_table[slot].used   = true;
    g_table[slot].is_dir = true;
    g_table[slot].data   = nullptr;
    g_table[slot].size   = 0;
    return true;
}

bool exists_dir(const char* name) {
    if (!name) return false;
    int i = find_name(name);
    return i >= 0 && g_table[i].is_dir;
}

const Entry* table() {
    return g_table;
}

}
