/*
  mmu.hpp - mmu init and query interface
  builds page tables, configures mair/tcr/ttbr0, enables mmu + caches
  call this early - before heap, gic, or any dma device
*/
#pragma once

namespace mmu {

void init();

bool enabled();

}
