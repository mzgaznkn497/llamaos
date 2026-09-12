#pragma once

#include "types.hpp"

// =============================================================================
// LlamaOS/A - Freestanding Memory and String Manipulation Library
// =============================================================================
// Implements freestanding C-compatible memory and string routines required by
// compiler intrinsics and kernel subsystem logic.
// =============================================================================

extern "C" {
    void* memset(void* dest, int ch, llamaos::size_t count);
    void* memcpy(void* dest, const void* src, llamaos::size_t count);
    void* memmove(void* dest, const void* src, llamaos::size_t count);
    int memcmp(const void* lhs, const void* rhs, llamaos::size_t count);

    llamaos::size_t strlen(const char* str);
    int strcmp(const char* lhs, const char* rhs);
    int strncmp(const char* lhs, const char* rhs, llamaos::size_t count);
    char* strcpy(char* dest, const char* src);
    char* strncpy(char* dest, const char* src, llamaos::size_t count);
    char* strchr(const char* str, int ch);
}

namespace llamaos {
    using ::memset;
    using ::memcpy;
    using ::memmove;
    using ::memcmp;
    using ::strlen;
    using ::strcmp;
    using ::strncmp;
    using ::strcpy;
    using ::strncpy;
    using ::strchr;
} // namespace llamaos
