#pragma once
#include "types.hpp"

namespace usersched
{
    void start_ab();                 // start two EL0 tasks printing A/B

    bool active();                   // syscall layer checks this

    // Called by syscall layer on SVCs while usersched is active.
    // Return value is a trapframe pointer (usually unchanged in our design).
    void* on_yield(void* frame, u64 elr); // elr = address of SVC instruction
    void* on_exit(void* frame, u64 code);
}
