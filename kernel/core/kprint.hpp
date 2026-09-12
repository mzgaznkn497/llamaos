#pragma once

#include "types.hpp"
#include <stdarg.h>

// =============================================================================
// LlamaOS/A - Kernel Printing and Logging Facility
// =============================================================================
// Provides unified output dispatching to Serial COM1, port 0xE9, and VGA console,
// featuring formatted printing and standardized log severity levels.
// =============================================================================

namespace llamaos {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Panic
};

void kprint_char(char c);
void kprint(const char* str);
void kprint_hex(uint64_t val, uint8_t width = 16);
void kprint_dec(uint64_t val);
void kprint_signed(int64_t val);

void kvprintf(const char* fmt, va_list args);
void kprintf(const char* fmt, ...);

void klog(LogLevel level, const char* fmt, ...);
void klog_debug(const char* fmt, ...);
void klog_info(const char* fmt, ...);
void klog_warn(const char* fmt, ...);
void klog_error(const char* fmt, ...);

} // namespace llamaos
