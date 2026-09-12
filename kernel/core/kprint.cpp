#include "kprint.hpp"
#include "drivers/serial.hpp"
#include "drivers/vga.hpp"
#include "string.hpp"

// =============================================================================
// LlamaOS/A - Kernel Printing and Logging Implementation
// =============================================================================

namespace llamaos {

using drivers::SerialPort;
using drivers::VgaConsole;
using drivers::VgaColor;

void kprint_char(char c) {
    SerialPort::put_char(c);
    VgaConsole::put_char(c);
}

void kprint(const char* str) {
    if (!str) return;
    while (*str) {
        kprint_char(*str++);
    }
}

void kprint_hex(uint64_t val, uint8_t width) {
    static const char hex_digits[] = "0123456789ABCDEF";
    kprint("0x");
    if (width == 0 || width > 16) width = 16;
    for (int i = (width - 1) * 4; i >= 0; i -= 4) {
        uint8_t nibble = static_cast<uint8_t>((val >> i) & 0x0F);
        kprint_char(hex_digits[nibble]);
    }
}

void kprint_dec(uint64_t val) {
    if (val == 0) {
        kprint_char('0');
        return;
    }
    char buf[32];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; --i) {
        kprint_char(buf[i]);
    }
}

void kprint_signed(int64_t val) {
    if (val < 0) {
        kprint_char('-');
        kprint_dec(static_cast<uint64_t>(-val));
    } else {
        kprint_dec(static_cast<uint64_t>(val));
    }
}

static void print_padded_hex(uint64_t val, int width) {
    static const char hex_digits[] = "0123456789abcdef";
    char buf[32];
    int idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        while (val > 0) {
            buf[idx++] = hex_digits[val & 0xF];
            val >>= 4;
        }
    }
    while (idx < width) {
        buf[idx++] = '0';
    }
    for (int i = idx - 1; i >= 0; --i) {
        kprint_char(buf[i]);
    }
}

void kvprintf(const char* fmt, va_list args) {
    if (!fmt) return;

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            kprint_char(fmt[i]);
            continue;
        }

        ++i;
        if (fmt[i] == '\0') break;

        // Parse optional padding/width
        bool pad_zero = false;
        int width = 0;
        if (fmt[i] == '0') {
            pad_zero = true;
            ++i;
        }
        while (fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + (fmt[i] - '0');
            ++i;
        }

        // Length specifiers (l, ll)
        bool is_long = false;
        bool is_long_long = false;
        if (fmt[i] == 'l') {
            is_long = true;
            ++i;
            if (fmt[i] == 'l') {
                is_long_long = true;
                ++i;
            }
        }

        switch (fmt[i]) {
            case 's': {
                const char* s = va_arg(args, const char*);
                kprint(s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = static_cast<char>(va_arg(args, int));
                kprint_char(c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t v;
                if (is_long_long || is_long) {
                    v = va_arg(args, int64_t);
                } else {
                    v = va_arg(args, int);
                }
                kprint_signed(v);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_long_long || is_long) {
                    v = va_arg(args, uint64_t);
                } else {
                    v = va_arg(args, unsigned int);
                }
                kprint_dec(v);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v;
                if (is_long_long || is_long) {
                    v = va_arg(args, uint64_t);
                } else {
                    v = va_arg(args, unsigned int);
                }
                if (width > 0 && pad_zero) {
                    print_padded_hex(v, width);
                } else {
                    kprint_hex(v, width > 0 ? static_cast<uint8_t>(width) : 8);
                }
                break;
            }
            case 'p': {
                uintptr_t v = reinterpret_cast<uintptr_t>(va_arg(args, void*));
                kprint_hex(v, 16);
                break;
            }
            case 'b': {
                bool v = static_cast<bool>(va_arg(args, int));
                kprint(v ? "true" : "false");
                break;
            }
            case '%': {
                kprint_char('%');
                break;
            }
            default: {
                kprint_char('%');
                kprint_char(fmt[i]);
                break;
            }
        }
    }
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}

void klog(LogLevel level, const char* fmt, ...) {
    switch (level) {
        case LogLevel::Debug:
            VgaConsole::set_color(VgaColor::DarkGray, VgaColor::Black);
            kprint("[DEBUG] ");
            break;
        case LogLevel::Info:
            VgaConsole::set_color(VgaColor::LightGreen, VgaColor::Black);
            kprint("[INFO]  ");
            break;
        case LogLevel::Warn:
            VgaConsole::set_color(VgaColor::Yellow, VgaColor::Black);
            kprint("[WARN]  ");
            break;
        case LogLevel::Error:
            VgaConsole::set_color(VgaColor::LightRed, VgaColor::Black);
            kprint("[ERROR] ");
            break;
        case LogLevel::Panic:
            VgaConsole::set_color(VgaColor::White, VgaColor::Red);
            kprint("[PANIC] ");
            break;
    }
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);

    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);

    kprint("\n");
}

void klog_debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VgaConsole::set_color(VgaColor::DarkGray, VgaColor::Black);
    kprint("[DEBUG] ");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
    kvprintf(fmt, args);
    va_end(args);
    kprint("\n");
}

void klog_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VgaConsole::set_color(VgaColor::LightGreen, VgaColor::Black);
    kprint("[INFO]  ");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
    kvprintf(fmt, args);
    va_end(args);
    kprint("\n");
}

void klog_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VgaConsole::set_color(VgaColor::Yellow, VgaColor::Black);
    kprint("[WARN]  ");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
    kvprintf(fmt, args);
    va_end(args);
    kprint("\n");
}

void klog_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VgaConsole::set_color(VgaColor::LightRed, VgaColor::Black);
    kprint("[ERROR] ");
    VgaConsole::set_color(VgaColor::LightGray, VgaColor::Black);
    kvprintf(fmt, args);
    va_end(args);
    kprint("\n");
}

} // namespace llamaos
