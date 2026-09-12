#include "types.hpp"
#include "panic.hpp"

// =============================================================================
// LlamaOS/A - Freestanding C++ Runtime and Security Primitives
// =============================================================================

extern "C" {

// Freestanding C++ ABI hooks
void __cxa_pure_virtual() {
    KPANIC("Pure virtual function called in freestanding kernel context!");
}

int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0; // Static destructors are not invoked in kernel lifetime
}

void* __dso_handle = nullptr;

// Stack Protector Canary (Security Hardening)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
llamaos::uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;
#pragma GCC diagnostic pop

[[noreturn]] void __stack_chk_fail() {
    KPANIC("Kernel Stack Smashing Detected! Canary check failed (__stack_chk_fail).");
}

} // extern "C"
