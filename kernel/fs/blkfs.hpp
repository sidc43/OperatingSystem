/*
  blkfs.hpp - block filesystem interface
  init() loads from disk (or returns false if no formatted disk)
  flush() saves all ramfs files back to disk
  ready() / formatted() for status checks
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace blkfs {

static constexpr uint32_t MAGIC        = 0x5346534Fu;
static constexpr uint32_t VERSION      = 1u;
static constexpr uint32_t HEADER_SEC   = 0u;
static constexpr uint32_t TABLE_SEC    = 1u;
static constexpr uint32_t TABLE_SECS   = 16u;
static constexpr uint32_t DATA_START   = 17u;
static constexpr size_t   ENTRY_SIZE   = 128u;
static constexpr size_t   MAX_ENTRIES  = 64u;

struct DiskHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t free_sector;
    uint8_t  pad[512 - 16];
} __attribute__((packed));

struct DiskEntry {
    char     name[64];
    uint32_t data_sector;
    uint32_t data_size;
    uint8_t  is_dir;
    uint8_t  used;
    uint8_t  pad[54];
} __attribute__((packed));

static_assert(sizeof(DiskHeader) == 512,       "DiskHeader must be 512 bytes");
static_assert(sizeof(DiskEntry)  == ENTRY_SIZE, "DiskEntry size mismatch");
static_assert(MAX_ENTRIES * ENTRY_SIZE == TABLE_SECS * 512u,
              "Entry table must fit exactly in TABLE_SECS sectors");

bool init (const uintptr_t* bases, int n);

bool flush();

bool ready();

}
