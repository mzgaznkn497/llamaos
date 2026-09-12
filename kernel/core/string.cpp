#include "string.hpp"

// =============================================================================
// Freestanding Implementation of Memory and String Primitives
// =============================================================================

extern "C" {

void* memset(void* dest, int ch, llamaos::size_t count) {
    auto* p = static_cast<unsigned char*>(dest);
    unsigned char val = static_cast<unsigned char>(ch);
    for (llamaos::size_t i = 0; i < count; ++i) {
        p[i] = val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, llamaos::size_t count) {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);
    for (llamaos::size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, llamaos::size_t count) {
    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);

    if (d < s) {
        for (llamaos::size_t i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (llamaos::size_t i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* lhs, const void* rhs, llamaos::size_t count) {
    const auto* a = static_cast<const unsigned char*>(lhs);
    const auto* b = static_cast<const unsigned char*>(rhs);
    for (llamaos::size_t i = 0; i < count; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

llamaos::size_t strlen(const char* str) {
    llamaos::size_t len = 0;
    while (str && str[len] != '\0') {
        ++len;
    }
    return len;
}

int strcmp(const char* lhs, const char* rhs) {
    if (!lhs && !rhs) return 0;
    if (!lhs) return -1;
    if (!rhs) return 1;

    while (*lhs && (*lhs == *rhs)) {
        ++lhs;
        ++rhs;
    }
    return static_cast<int>(static_cast<unsigned char>(*lhs)) -
           static_cast<int>(static_cast<unsigned char>(*rhs));
}

int strncmp(const char* lhs, const char* rhs, llamaos::size_t count) {
    if (count == 0) return 0;
    if (!lhs && !rhs) return 0;
    if (!lhs) return -1;
    if (!rhs) return 1;

    for (llamaos::size_t i = 0; i < count; ++i) {
        if (lhs[i] != rhs[i]) {
            return static_cast<int>(static_cast<unsigned char>(lhs[i])) -
                   static_cast<int>(static_cast<unsigned char>(rhs[i]));
        }
        if (lhs[i] == '\0') {
            break;
        }
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* orig = dest;
    while ((*dest++ = *src++) != '\0') {}
    return orig;
}

char* strncpy(char* dest, const char* src, llamaos::size_t count) {
    if (!dest) return dest;
    llamaos::size_t i = 0;
    for (; i < count && src && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < count; ++i) {
        dest[i] = '\0';
    }
    return dest;
}

const char* strchr(const char* str, int ch) {
    if (!str) return nullptr;
    char target = static_cast<char>(ch);
    while (*str) {
        if (*str == target) return str;
        ++str;
    }
    return (target == '\0') ? str : nullptr;
}

} // extern "C"
