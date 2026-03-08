/*
  uart_pl011.cpp - pl011 uart driver for the qemu virt board (0x09000000)
  115200 8n1, polled (no interrupts)
  putc spins on tx fifo full, getc returns -1 if rx fifo is empty
*/
#include "kernel/drivers/uart_pl011.hpp"
#include <stdint.h>

namespace uart {

static constexpr uintptr_t BASE = 0x09000000;

static constexpr uint32_t DR    = 0x000;
static constexpr uint32_t FR    = 0x018;
static constexpr uint32_t IBRD  = 0x024;
static constexpr uint32_t FBRD  = 0x028;
static constexpr uint32_t LCRH  = 0x02C;
static constexpr uint32_t CR    = 0x030;
static constexpr uint32_t IMSC  = 0x038;
static constexpr uint32_t ICR   = 0x044;

static constexpr uint32_t FR_RXFE = (1u << 4);
static constexpr uint32_t FR_TXFF = (1u << 5);
static constexpr uint32_t FR_BUSY = (1u << 3);

static constexpr uint32_t LCRH_FEN  = (1u << 4);
static constexpr uint32_t LCRH_WLEN_8 = (3u << 5);

static constexpr uint32_t CR_UARTEN = (1u << 0);
static constexpr uint32_t CR_TXE    = (1u << 8);
static constexpr uint32_t CR_RXE    = (1u << 9);

static inline volatile uint32_t* reg(uint32_t off) {
    return reinterpret_cast<volatile uint32_t*>(BASE + off);
}

void init() {

    *reg(CR) = 0;

    while (*reg(FR) & FR_BUSY) {}

    *reg(LCRH) = 0;

    *reg(ICR) = 0x7FF;

    *reg(IBRD) = 13;
    *reg(FBRD) = 1;

    *reg(LCRH) = LCRH_WLEN_8 | LCRH_FEN;

    *reg(IMSC) = 0;

    *reg(CR) = CR_UARTEN | CR_TXE | CR_RXE;
}

void putc(char c) {

    while (*reg(FR) & FR_TXFF) {}
    *reg(DR) = static_cast<uint32_t>(c);
}

int getc() {

    if (*reg(FR) & FR_RXFE) return -1;
    return static_cast<int>(*reg(DR) & 0xFF);
}

}
