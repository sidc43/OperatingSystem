#pragma once
#include "types.hpp"

namespace syscall
{
    // Returns the trapframe pointer that vectors.S should restore (may switch threads)
    void* handle_svc(u64 esr, u64 elr, void* frame);
}
