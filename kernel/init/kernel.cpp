#include "drivers/console/console.hpp"
#include "kernel/core/print.hpp"
#include "kernel/core/panic.hpp"

#include "kernel/mm/phys/page_alloc.hpp"
#include "kernel/mm/vm/vm.hpp"
#include "kernel/mm/heap/kheap.hpp"

#include "kernel/tests/usermode_tests.hpp"

extern "C" void kernel_main(u64)
{
    console::init();
    phys::init();

    vm::init();
    vm::switch_to_kernel_table();

    kheap::init();

    kprint::puts("\n[kernel] starting EL0 smoke test...\n");
    tests::usermode_smoke_test();

    panic("kernel_main: returned from usermode_smoke_test");
}