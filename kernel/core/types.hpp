#pragma once

// =============================================================================
// LlamaOS/A - Freestanding Core Primitive Types
// =============================================================================
// Provides standard fixed-width integer types and size definitions for the
// freestanding x86-64 kernel environment without relying on hosted standard headers.
// =============================================================================

namespace llamaos {

// Fixed-width integer types
using int8_t   = signed char;
using uint8_t  = unsigned char;
using int16_t  = short;
using uint16_t = unsigned short;
using int32_t  = int;
using uint32_t = unsigned int;
using int64_t  = long long;
using uint64_t = unsigned long long;

// Pointer-sized and pointer arithmetic types
using intptr_t  = long long;
using uintptr_t = unsigned long long;
using ptrdiff_t = long long;
using size_t    = unsigned long long;
using ssize_t   = long long;

// Boolean type
static_assert(sizeof(bool) == 1, "bool must be 1 byte");
static_assert(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
static_assert(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
static_assert(sizeof(uintptr_t) == 8, "uintptr_t must be 8 bytes for x86-64");

// Address spaces
constexpr uintptr_t KERNEL_VIRTUAL_BASE = 0xFFFFFFFF80000000ULL;
constexpr uintptr_t PAGE_SIZE           = 4096ULL;
constexpr uintptr_t LARGE_PAGE_SIZE     = 2 * 1024 * 1024ULL; // 2 MiB

// Utility function to convert physical address to higher-half kernel virtual address
inline constexpr uintptr_t phys_to_virt(uintptr_t phys) {
    return phys + KERNEL_VIRTUAL_BASE;
}

// Utility function to convert higher-half kernel virtual address to physical address
inline constexpr uintptr_t virt_to_phys(uintptr_t virt) {
    return virt - KERNEL_VIRTUAL_BASE;
}

// Memory alignment helper
template <typename T>
inline constexpr T align_up(T value, T alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
inline constexpr T align_down(T value, T alignment) {
    return value & ~(alignment - 1);
}

} // namespace llamaos

// Export to global scope for standard freestanding prototypes
using llamaos::int8_t;
using llamaos::uint8_t;
using llamaos::int16_t;
using llamaos::uint16_t;
using llamaos::int32_t;
using llamaos::uint32_t;
using llamaos::int64_t;
using llamaos::uint64_t;
using llamaos::intptr_t;
using llamaos::uintptr_t;
using llamaos::size_t;
using llamaos::ssize_t;
using llamaos::ptrdiff_t;

