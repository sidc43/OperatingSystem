/*
  regs.hpp - inline helpers for aarch64 system registers and memory barriers
  SYSREG_READ/SYSREG_WRITE macros, named inlines for commonly used registers
  (esr, far, elr, spsr, cntfrq, cntpct, cntp_tval, cntp_ctl)
  dsb_sy, dmb_ish, isb barriers
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SYSREG_READ(reg) \
  ([](){ uint64_t v; asm volatile("mrs %0, " #reg : "=r"(v)); return v; }())

#define SYSREG_WRITE(reg, val) \
  do { asm volatile("msr " #reg ", %0" :: "r"((uint64_t)(val))); } while(0)

static inline uint64_t read_currentel()   { return SYSREG_READ(CurrentEL); }
static inline uint64_t read_esr_el1()     { return SYSREG_READ(esr_el1);   }
static inline uint64_t read_far_el1()     { return SYSREG_READ(far_el1);   }
static inline uint64_t read_elr_el1()     { return SYSREG_READ(elr_el1);   }
static inline uint64_t read_spsr_el1()    { return SYSREG_READ(spsr_el1);  }
static inline uint64_t read_cntfrq_el0()  { return SYSREG_READ(cntfrq_el0);}
static inline uint64_t read_cntpct_el0()  { return SYSREG_READ(cntpct_el0);}

static inline void write_cntp_tval_el0(uint64_t v) { SYSREG_WRITE(cntp_tval_el0, v); }
static inline void write_cntp_ctl_el0(uint64_t v)  { SYSREG_WRITE(cntp_ctl_el0, v);  }
static inline uint64_t read_cntp_ctl_el0()         { return SYSREG_READ(cntp_ctl_el0); }

static inline void dsb_sy()  { asm volatile("dsb sy"  ::: "memory"); }
static inline void dmb_ish() { asm volatile("dmb ish" ::: "memory"); }
static inline void isb()     { asm volatile("isb"     ::: "memory"); }

static inline void irq_enable()  { asm volatile("msr daifclr, #2" ::: "memory"); }
static inline void irq_disable() { asm volatile("msr daifset, #2" ::: "memory"); }

static inline void dc_civac_range(const void* p, size_t n) {
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)63;
    uintptr_t e = (uintptr_t)p + n;
    for (; a < e; a += 64)
        asm volatile("dc civac, %0" :: "r"(a) : "memory");
    asm volatile("dsb sy" ::: "memory");
}

static inline void dc_ivac_range(const void* p, size_t n) {
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)63;
    uintptr_t e = (uintptr_t)p + n;
    for (; a < e; a += 64)
        asm volatile("dc ivac, %0" :: "r"(a) : "memory");
    asm volatile("dsb sy" ::: "memory");
}
