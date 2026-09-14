#pragma once

// =============================================================================
// LlamaOS/A - Freestanding Core Primitive Types
// =============================================================================
// Provides standard fixed-width integer types and size definitions for the
// freestanding x86-64 kernel environment without relying on hosted standard headers.
// =============================================================================

namespace llamaos {

// Fixed-width integer types matched with compiler ABI
using int8_t   = __INT8_TYPE__;
using uint8_t  = __UINT8_TYPE__;
using int16_t  = __INT16_TYPE__;
using uint16_t = __UINT16_TYPE__;
using int32_t  = __INT32_TYPE__;
using uint32_t = __UINT32_TYPE__;
using int64_t  = __INT64_TYPE__;
using uint64_t = __UINT64_TYPE__;

// Pointer-sized and pointer arithmetic types
using intptr_t  = __INTPTR_TYPE__;
using uintptr_t = __UINTPTR_TYPE__;
using ptrdiff_t = __PTRDIFF_TYPE__;
using size_t    = __SIZE_TYPE__;
using ssize_t   = __PTRDIFF_TYPE__;

// Boolean type
static_assert(sizeof(bool) == 1, "bool must be 1 byte");
static_assert(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
static_assert(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
static_assert(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
static_assert(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
static_assert(sizeof(uintptr_t) == 8, "uintptr_t must be 8 bytes for x86-64");

// Integer limit constants
constexpr uint8_t  UINT8_MAX  = 0xFFU;
constexpr uint16_t UINT16_MAX = 0xFFFFU;
constexpr uint32_t UINT32_MAX = 0xFFFFFFFFU;
constexpr uint64_t UINT64_MAX = 0xFFFFFFFFFFFFFFFFULL;
constexpr uintptr_t UINTPTR_MAX = 0xFFFFFFFFFFFFFFFFULL;
constexpr intptr_t  INTPTR_MAX  = 0x7FFFFFFFFFFFFFFFLL;

constexpr int8_t   INT8_MAX   = 0x7F;
constexpr int8_t   INT8_MIN   = -0x80;
constexpr int16_t  INT16_MAX  = 0x7FFF;
constexpr int16_t  INT16_MIN  = -0x8000;
constexpr int32_t  INT32_MAX  = 0x7FFFFFFF;
constexpr int32_t  INT32_MIN  = -0x7FFFFFFF - 1;
constexpr int64_t  INT64_MAX  = 0x7FFFFFFFFFFFFFFFLL;
constexpr int64_t  INT64_MIN  = -0x7FFFFFFFFFFFFFFFLL - 1LL;

// Address spaces
constexpr uintptr_t KERNEL_VIRTUAL_BASE = 0xFFFFFFFF80000000ULL;
constexpr uintptr_t PAGE_SIZE           = 4096ULL;
constexpr uintptr_t LARGE_PAGE_SIZE     = 2 * 1024 * 1024ULL; // 2 MiB

// Address translation helpers (Valid for physical addresses 0x0 .. 0x7FFFFFFF mapped to -2 GiB .. 0)
inline constexpr uintptr_t phys_to_virt(uintptr_t phys) {
    return phys + KERNEL_VIRTUAL_BASE;
}

inline constexpr uintptr_t virt_to_phys(uintptr_t virt) {
    return virt - KERNEL_VIRTUAL_BASE;
}

// Memory alignment helpers with integer overflow protection
template <typename T>
inline constexpr bool align_up_checked(T value, T alignment, T& out) {
    if (alignment == 0) {
        out = value;
        return true;
    }
    T rem = value % alignment;
    if (rem == 0) {
        out = value;
        return true;
    }
    T delta = alignment - rem;
    if (value > static_cast<T>(~static_cast<T>(0)) - delta) {
        return false; // Overflow would occur
    }
    out = value + delta;
    return true;
}

template <typename T>
inline constexpr T align_up(T value, T alignment) {
    T res = value;
    if (!align_up_checked(value, alignment, res)) {
        return static_cast<T>(~static_cast<T>(0)); // Saturate on overflow
    }
    return res;
}

template <typename T>
inline constexpr T align_down(T value, T alignment) {
    if (alignment == 0) return value;
    return value - (value % alignment);
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

using llamaos::UINT8_MAX;
using llamaos::UINT16_MAX;
using llamaos::UINT32_MAX;
using llamaos::UINT64_MAX;
using llamaos::UINTPTR_MAX;
using llamaos::INT8_MAX;
using llamaos::INT16_MAX;
using llamaos::INT32_MAX;
using llamaos::INT64_MAX;
using llamaos::INTPTR_MAX;

