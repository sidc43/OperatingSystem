#pragma once
#include "types.hpp"

namespace vm_fault
{
    bool try_handle(u64 vector_id, u64 esr, u64 elr, u64 far);
    void dump_and_panic(u64 vector_id, u64 esr, u64 elr, u64 far);
}
