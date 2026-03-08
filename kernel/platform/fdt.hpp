/*
  fdt.hpp - fdt/dtb scanner interface
  valid() checks if a pointer looks like a real dtb
  collect_virtio_mmio_regs() pulls out the base addresses of all virtio,mmio nodes
*/
#pragma once
#include <stdint.h>

namespace fdt {

bool valid(const void* dtb);

int collect_virtio_mmio_regs(const void* dtb, uintptr_t* out, int max);

}
