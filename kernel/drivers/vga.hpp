#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Text-Mode VGA Console Driver
// =============================================================================
// Manages the standard 80x25 alphanumeric text-mode buffer at 0xB8000.
// =============================================================================

namespace llamaos::drivers {

enum class VgaColor : uint8_t {
    Black         = 0,
    Blue          = 1,
    Green         = 2,
    Cyan          = 3,
    Red           = 4,
    Magenta       = 5,
    Brown         = 6,
    LightGray     = 7,
    DarkGray      = 8,
    LightBlue     = 9,
    LightGreen    = 10,
    LightCyan     = 11,
    LightRed      = 12,
    LightMagenta  = 13,
    Yellow        = 14,
    White         = 15,
};

class VgaConsole {
public:
    static constexpr size_t VGA_WIDTH = 80;
    static constexpr size_t VGA_HEIGHT = 25;
    static constexpr uintptr_t VGA_BUFFER_PHYS = 0x000B8000;

    static void init();
    static void clear();
    static void set_color(VgaColor fg, VgaColor bg);
    static void put_char(char c);
    static void write(const char* str);
    static void write(const char* str, size_t length);
    static void update_cursor();
    static void set_cursor(size_t col, size_t row);

private:
    static inline volatile uint16_t* s_buffer = reinterpret_cast<volatile uint16_t*>(phys_to_virt(VGA_BUFFER_PHYS));
    static inline size_t s_row = 0;
    static inline size_t s_col = 0;
    static inline uint8_t s_color = 0x07; // Light gray on black
    static inline bool s_initialized = false;

    static void scroll();
    static uint16_t make_entry(char c, uint8_t color) {
        return static_cast<uint16_t>(static_cast<uint8_t>(c)) | (static_cast<uint16_t>(color) << 8);
    }
};

} // namespace llamaos::drivers
