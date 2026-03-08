/*
  blk.hpp - virtio-blk driver interface
  init/ready/sector_count/read_sectors/write_sectors
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace vblk {

bool     init         (const uintptr_t* bases, int n);

bool     ready        ();

uint64_t sector_count ();

bool read_sectors (uint64_t lba, uint32_t count, void*       buf);

bool write_sectors(uint64_t lba, uint32_t count, const void* buf);

}
