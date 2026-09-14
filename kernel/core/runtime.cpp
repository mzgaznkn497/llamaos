#include "types.hpp"
#include "panic.hpp"

// =============================================================================
// LlamaOS/A - Freestanding C++ Runtime and Security Primitives
// =============================================================================

extern "C" {

// Static global constructor array boundaries defined in linker script
extern void (*_init_array_start[])() __attribute__((weak));
extern void (*_init_array_end[])() __attribute__((weak));

void call_global_constructors() {
    if (&_init_array_start == nullptr || &_init_array_end == nullptr) {
        return;
    }
    for (void (**ctor)() = _init_array_start; ctor < _init_array_end; ++ctor) {
        if (*ctor) {
            (*ctor)();
        }
    }
}

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

// Freestanding placement new definitions
void* operator new(llamaos::size_t, void* ptr) noexcept {
    return ptr;
}

void* operator new[](llamaos::size_t, void* ptr) noexcept {
    return ptr;
}

// Deterministic panicking stubs for unsupported freestanding heap deallocation
void operator delete(void*) noexcept {
    KPANIC("Dynamic memory deallocation (operator delete) is not supported in Phase 1!");
}

void operator delete[](void*) noexcept {
    KPANIC("Dynamic memory deallocation (operator delete[]) is not supported in Phase 1!");
}

void operator delete(void*, llamaos::size_t) noexcept {
    KPANIC("Sized dynamic memory deallocation (operator delete) is not supported in Phase 1!");
}

void operator delete[](void*, llamaos::size_t) noexcept {
    KPANIC("Sized dynamic memory deallocation (operator delete[]) is not supported in Phase 1!");
}
