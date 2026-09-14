#include "user_memory.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - User Memory Boundary Validator Implementation
// =============================================================================

namespace llamaos::userland {

bool UserMemoryValidator::validate_read_buffer(const void* ptr, size_t length) noexcept {
    if (!ptr || length == 0) return false;
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);

    if (!is_user_range(start, length)) {
        return false;
    }

    uintptr_t end = start + length;
    uintptr_t page_start = start & ~(PAGE_SIZE - 1);
    uintptr_t page_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t va_val = page_start; va_val < page_end; va_val += PAGE_SIZE) {
        memory::VirtualAddress va(va_val);
        memory::PhysicalAddress pa;
        memory::PageFlags flags;

        // Query active VMM hardware page table hierarchy
        if (!memory::g_vmm.translate(va, &pa, &flags)) {
            return false;
        }

        if (!test_flag(flags, memory::PageFlags::Present)) {
            return false;
        }

        if (!test_flag(flags, memory::PageFlags::User)) {
            return false; // Supervisor-only page
        }
    }

    return true;
}

bool UserMemoryValidator::validate_write_buffer(void* ptr, size_t length) noexcept {
    if (!ptr || length == 0) return false;
    uintptr_t start = reinterpret_cast<uintptr_t>(ptr);

    if (!is_user_range(start, length)) {
        return false;
    }

    uintptr_t end = start + length;
    uintptr_t page_start = start & ~(PAGE_SIZE - 1);
    uintptr_t page_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t va_val = page_start; va_val < page_end; va_val += PAGE_SIZE) {
        memory::VirtualAddress va(va_val);
        memory::PhysicalAddress pa;
        memory::PageFlags flags;

        if (!memory::g_vmm.translate(va, &pa, &flags)) {
            return false;
        }

        if (!test_flag(flags, memory::PageFlags::Present)) {
            return false;
        }

        if (!test_flag(flags, memory::PageFlags::User)) {
            return false; // Supervisor-only page
        }

        if (!test_flag(flags, memory::PageFlags::Writable)) {
            return false; // Page is not writable
        }
    }

    return true;
}

bool UserMemoryValidator::validate_string(const char* str, size_t max_len, size_t* out_len) noexcept {
    if (!str || max_len == 0) return false;
    uintptr_t start = reinterpret_cast<uintptr_t>(str);
    if (!is_user_address(start)) return false;

    size_t len = 0;
    while (len < max_len) {
        uintptr_t current_addr = start + len;
        if (current_addr > USER_ADDR_MAX) return false;

        memory::VirtualAddress page_va(current_addr & ~(PAGE_SIZE - 1));
        memory::PhysicalAddress pa;
        memory::PageFlags flags;
        if (!memory::g_vmm.translate(page_va, &pa, &flags) ||
            !test_flag(flags, memory::PageFlags::Present) ||
            !test_flag(flags, memory::PageFlags::User)) {
            return false;
        }

        if (str[len] == '\0') {
            if (out_len) *out_len = len;
            return true;
        }
        len++;
    }

    return false; // Not null-terminated within max_len
}

} // namespace llamaos::userland
