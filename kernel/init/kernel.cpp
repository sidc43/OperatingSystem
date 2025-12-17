#include "drivers/console/console.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#include "kernel/mm/phys/page_alloc.hpp"
#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/heap/kheap.hpp"

#include "drivers/interrupt/gicv2/gicv2.hpp"
#include "drivers/timer/arch_timer.hpp"
#include "kernel/irq/irq.hpp"

#include "kernel/usermode/usersched.hpp"

extern "C" void kernel_main(u64)
{
    console::init();
    phys::init();

    vm::init();
    vm::switch_to_kernel_table();

    kheap::init();

    gicv2::init();
    gicv2::enable_int(30);

    arch_timer::init_100hz();
    irq::enable();

    kprint::puts("\n[kernel] timer heartbeat (expect dots)...\n");

    usersched::start_ab();

    panic("kernel_main: usersched returned unexpectedly");
}
