/*
  gic.cpp - gicv2 interrupt controller driver for qemu virt
  distributor at 0x08000000, cpu interface at 0x08010000
  supports registering c handlers per irq line and dispatching from exception entry
*/
#include "kernel/irq/gic.hpp"
#include "kernel/core/panic.hpp"
#include "kernel/core/print.hpp"
#include <stdint.h>

namespace gic {

static constexpr uintptr_t GICD_BASE = 0x08000000;
static constexpr uintptr_t GICC_BASE = 0x08010000;

static constexpr uint32_t GICD_CTLR        = 0x000;
static constexpr uint32_t GICD_TYPER       = 0x004;
static constexpr uint32_t GICD_IGROUPR0    = 0x080;
static constexpr uint32_t GICD_ISENABLER0  = 0x100;
static constexpr uint32_t GICD_ICENABLER0  = 0x180;
static constexpr uint32_t GICD_IPRIORITYR0 = 0x400;
static constexpr uint32_t GICD_ITARGETSR0  = 0x800;
[[maybe_unused]] static constexpr uint32_t GICD_ICFGR0 = 0xC00;

static constexpr uint32_t GICC_CTLR = 0x000;
static constexpr uint32_t GICC_PMR  = 0x004;
static constexpr uint32_t GICC_BPR  = 0x008;
static constexpr uint32_t GICC_IAR  = 0x00C;
static constexpr uint32_t GICC_EOIR = 0x010;

static constexpr uint32_t MAX_IRQ = 256;

static Handler g_handlers[MAX_IRQ] = {};

static inline volatile uint32_t* gicd(uint32_t off) {
    return reinterpret_cast<volatile uint32_t*>(GICD_BASE + off);
}
static inline volatile uint32_t* gicc(uint32_t off) {
    return reinterpret_cast<volatile uint32_t*>(GICC_BASE + off);
}

void init() {

    *gicd(GICD_CTLR) = 0;

    uint32_t typer = *gicd(GICD_TYPER);
    uint32_t n_irqs = ((typer & 0x1F) + 1) * 32;
    if (n_irqs > MAX_IRQ) n_irqs = MAX_IRQ;

    uint32_t n_words = n_irqs / 32;

    for (uint32_t i = 0; i < n_words; i++) {
        *gicd(GICD_IGROUPR0   + i * 4) = 0x00000000;
        *gicd(GICD_ICENABLER0 + i * 4) = 0xFFFFFFFF;
    }

    for (uint32_t i = 0; i < n_irqs; i++) {

        volatile uint8_t* prio = reinterpret_cast<volatile uint8_t*>(
            GICD_BASE + GICD_IPRIORITYR0 + i);
        *prio = 0xA0;

        if (i >= 32) {
            volatile uint8_t* tgt = reinterpret_cast<volatile uint8_t*>(
                GICD_BASE + GICD_ITARGETSR0 + i);
            *tgt = 0x01;
        }
    }

    *gicd(GICD_CTLR) = 0x1;

    *gicc(GICC_PMR) = 0xFF;

    *gicc(GICC_BPR) = 0x00;

    *gicc(GICC_CTLR) = 0x1;

    printk("gic: init done (n_irqs=%u)\n", (unsigned)n_irqs);
}

void enable_irq(uint32_t irq) {
    if (irq >= MAX_IRQ) return;

    *gicd(GICD_ISENABLER0 + (irq / 32) * 4) = (1u << (irq & 31));
}

void disable_irq(uint32_t irq) {
    if (irq >= MAX_IRQ) return;
    *gicd(GICD_ICENABLER0 + (irq / 32) * 4) = (1u << (irq & 31));
}

void set_priority(uint32_t irq, uint8_t prio) {
    if (irq >= MAX_IRQ) return;
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(
        GICD_BASE + GICD_IPRIORITYR0 + irq);
    *p = prio;
}

void register_handler(uint32_t irq, Handler h) {
    if (irq >= MAX_IRQ) panic("gic: irq out of range", irq);
    g_handlers[irq] = h;
}

extern "C" void dispatch() {

    uint32_t iar = *gicc(GICC_IAR);
    uint32_t irq = iar & 0x3FF;

    if (irq == 1023) {
        *gicc(GICC_EOIR) = iar;
        return;
    }

    if (irq < MAX_IRQ && g_handlers[irq]) {
        g_handlers[irq]();
    } else {
        printk("gic: unhandled irq %u\n", (unsigned)irq);
    }

    *gicc(GICC_EOIR) = iar;
}

}
