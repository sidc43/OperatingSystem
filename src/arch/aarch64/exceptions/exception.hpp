#pragma once
#include "types.hpp"

extern "C" void exception_dispatch(u64 vector_id, u64 esr, u64 elr, u64 far, void* frame);