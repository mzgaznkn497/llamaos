#pragma once

#include "types.hpp"

// =============================================================================
// LlamaOS/A - Deterministic Kernel Panic Subsystem
// =============================================================================
// Halts the CPU deterministically upon unrecoverable condition, reporting
// failure location, diagnosis string, and architectural control registers.
// =============================================================================

namespace llamaos {

[[noreturn]] void panic_handler(const char* file, int line, const char* function, const char* message);

#define KPANIC(msg) ::llamaos::panic_handler(__FILE__, __LINE__, __func__, msg)

#define KASSERT(cond) \
    do { \
        if (!(cond)) { \
            ::llamaos::panic_handler(__FILE__, __LINE__, __func__, "Assertion failed: " #cond); \
        } \
    } while (0)

} // namespace llamaos
