#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "memory/memory_layout.hpp"

// =============================================================================
// LlamaOS/A - Phase 7 User Memory Boundary & Pointer Validator
// =============================================================================
// Strictly validates all pointers, buffers, and string parameters originating
// from Ring 3 userland before kernel dereference:
// - Verifies canonical 48-bit lower-half addressing (0x1000 .. 0x7FFFFFFFFFFF)
// - Defends against integer overflow in pointer arithmetic (ptr + len)
// - Walks hardware page table hierarchy (active CR3) to verify Present and User bits
// - Enforces Writable permission if buffer write is required
// =============================================================================

namespace llamaos::userland {

class UserMemoryValidator {
public:
    static constexpr uint64_t USER_ADDR_MIN = 0x0000000000001000ULL; // 4 KiB Null-page protection
    static constexpr uint64_t USER_ADDR_MAX = 0x00007FFFFFFFFFFFULL; // 128 TiB Lower-half limit
    static constexpr size_t   MAX_USER_BUFFER_SIZE = 16 * 1024 * 1024; // 16 MiB ceiling

    // Returns true if the address is canonical and resides in the user lower-half (above null-page)
    [[nodiscard]] static constexpr bool is_user_address(uintptr_t addr) noexcept {
        return (addr >= USER_ADDR_MIN) && (addr <= USER_ADDR_MAX);
    }

    // Returns true if the interval [start, start + length) is safely within user address space
    [[nodiscard]] static bool is_user_range(uintptr_t start, size_t length) noexcept {
        if (length == 0) return false;
        if (length > MAX_USER_BUFFER_SIZE) return false;
        if (!is_user_address(start)) return false;

        // Check for integer overflow
        uintptr_t end = start + length;
        if (end < start) return false;

        // Last byte must reside in canonical lower-half user address space
        uintptr_t last_byte = end - 1;
        if (last_byte > USER_ADDR_MAX) return false;

        return true;
    }

    // Authoritative verification methods
    [[nodiscard]] static bool validate_read_buffer(const void* ptr, size_t length) noexcept;
    [[nodiscard]] static bool validate_write_buffer(void* ptr, size_t length) noexcept;
    [[nodiscard]] static bool validate_string(const char* str, size_t max_len, size_t* out_len = nullptr) noexcept;

    template <typename T>
    [[nodiscard]] static bool validate_object_read(const T* ptr) noexcept {
        if (!ptr) return false;
        return validate_read_buffer(reinterpret_cast<const void*>(ptr), sizeof(T));
    }

    template <typename T>
    [[nodiscard]] static bool validate_object_write(T* ptr) noexcept {
        if (!ptr) return false;
        return validate_write_buffer(reinterpret_cast<void*>(ptr), sizeof(T));
    }

    // Compatibility wrappers
    [[nodiscard]] static bool validate_user_buffer(const void* ptr, size_t length, bool require_writable) noexcept {
        return require_writable ? validate_write_buffer(const_cast<void*>(ptr), length)
                                : validate_read_buffer(ptr, length);
    }

    [[nodiscard]] static bool validate_user_string(const char* str, size_t max_len, size_t* out_len = nullptr) noexcept {
        return validate_string(str, max_len, out_len);
    }
};

} // namespace llamaos::userland
