/*
  mmu.cpp - sets up the aarch64 mmu and enables caches
  identity-maps the full address space: mmio (0x0-0x3fffffff) as device-ngnrnre,
  ram (0x40000000-0x7fffffff) as normal wb cached
  carves out a guard page below the stack bottom so stack overflow triggers a fault
  after mmu::init() instruction and data caches are on
*/
#include "kernel/mm/mmu.hpp"
#include "kernel/core/panic.hpp"
#include "arch/aarch64/regs.hpp"
#include <stdint.h>
#include <stddef.h>

extern "C" uint8_t __guard_start[];
extern "C" uint8_t __guard_end[];

namespace {

alignas(4096) static uint64_t l1_table[512];
alignas(4096) static uint64_t l2_table[512];
alignas(4096) static uint64_t l3_guard_table[512];

static bool g_enabled = false;

static constexpr uint64_t PTE_BLOCK  = 1ULL;
static constexpr uint64_t PTE_TABLE  = 3ULL;
static constexpr uint64_t PTE_AF     = 1ULL << 10;
static constexpr uint64_t PTE_SH_IS  = 3ULL << 8;

static constexpr uint64_t PTE_ATTR0  = 0ULL << 2;
static constexpr uint64_t PTE_ATTR1  = 1ULL << 2;

static uint64_t normal_block(uint64_t pa) {
    return pa | PTE_BLOCK | PTE_AF | PTE_SH_IS | PTE_ATTR0;
}

static uint64_t device_block(uint64_t pa) {
    return pa | PTE_BLOCK | PTE_AF | PTE_ATTR1;

}

static uint64_t table_ptr(const uint64_t* p) {
    return (uint64_t)(uintptr_t)p | PTE_TABLE;
}

static uint64_t normal_page(uint64_t pa) {

    return pa | PTE_TABLE | PTE_AF | PTE_SH_IS | PTE_ATTR0;
}

static void build_tables() {

    l1_table[0] = device_block(0x00000000ULL);

    l1_table[1] = table_ptr(l2_table);

    for (int i = 0; i < 512; ++i) {
        uint64_t pa = 0x40000000ULL + (uint64_t)i * 0x200000ULL;
        l2_table[i] = normal_block(pa);
    }

    uintptr_t guard_pa = (uintptr_t)__guard_start;

    uint64_t l2_idx = (guard_pa - 0x40000000ULL) >> 21;
    if (l2_idx >= 512)
        panic("mmu: guard page outside L2 range");

    uint64_t l3_idx = (guard_pa >> 12) & 0x1FFull;

    uint64_t block_pa = 0x40000000ULL + l2_idx * 0x200000ULL;

    for (int i = 0; i < 512; ++i) {
        uint64_t pa = block_pa + (uint64_t)i * 0x1000ULL;
        l3_guard_table[i] = normal_page(pa);
    }

    l3_guard_table[l3_idx] = 0ULL;

    l2_table[l2_idx] = table_ptr(l3_guard_table);
}

}

namespace mmu {

void init() {

    build_tables();

    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb"    ::: "memory");

    SYSREG_WRITE(mair_el1, 0x00FFull);
    asm volatile("isb" ::: "memory");

    static constexpr uint64_t TCR =
          25ULL
        | (1ULL << 8)
        | (1ULL << 10)
        | (3ULL << 12)
        | (1ULL << 23);
    SYSREG_WRITE(tcr_el1, TCR);
    asm volatile("isb" ::: "memory");

    SYSREG_WRITE(ttbr0_el1, (uint64_t)(uintptr_t)l1_table);
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb"    ::: "memory");

    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("dsb sy"       ::: "memory");
    asm volatile("isb"          ::: "memory");

    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0)
           | (1ULL << 2)
           | (1ULL << 12);
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
    asm volatile("isb" ::: "memory");

    g_enabled = true;
}

bool enabled() { return g_enabled; }

}
