#include "vga.hpp"
#include "arch/x86_64/cpu/io.hpp"

// =============================================================================
// LlamaOS/A - Text-Mode VGA Console Implementation
// =============================================================================

namespace llamaos::drivers {

using namespace arch::x86_64;

void VgaConsole::init() {
    s_buffer = reinterpret_cast<volatile uint16_t*>(phys_to_virt(VGA_BUFFER_PHYS));
    s_row = 0;
    s_col = 0;
    s_color = (static_cast<uint8_t>(VgaColor::Black) << 4) | static_cast<uint8_t>(VgaColor::LightGray);
    clear();
    s_initialized = true;
}

void VgaConsole::clear() {
    uint16_t blank = make_entry(' ', s_color);
    for (size_t y = 0; y < VGA_HEIGHT; ++y) {
        for (size_t x = 0; x < VGA_WIDTH; ++x) {
            s_buffer[y * VGA_WIDTH + x] = blank;
        }
    }
    s_row = 0;
    s_col = 0;
    update_cursor();
}

void VgaConsole::set_color(VgaColor fg, VgaColor bg) {
    s_color = (static_cast<uint8_t>(bg) << 4) | static_cast<uint8_t>(fg);
}

void VgaConsole::scroll() {
    uint16_t blank = make_entry(' ', s_color);
    for (size_t y = 1; y < VGA_HEIGHT; ++y) {
        for (size_t x = 0; x < VGA_WIDTH; ++x) {
            s_buffer[(y - 1) * VGA_WIDTH + x] = s_buffer[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; ++x) {
        s_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
    s_row = VGA_HEIGHT - 1;
}

void VgaConsole::put_char(char c) {
    if (c == '\n') {
        s_col = 0;
        if (++s_row >= VGA_HEIGHT) {
            scroll();
        }
    } else if (c == '\r') {
        s_col = 0;
    } else if (c == '\t') {
        s_col = (s_col + 8) & ~7;
        if (s_col >= VGA_WIDTH) {
            s_col = 0;
            if (++s_row >= VGA_HEIGHT) {
                scroll();
            }
        }
    } else if (c == '\b') {
        if (s_col > 0) {
            s_col--;
            s_buffer[s_row * VGA_WIDTH + s_col] = make_entry(' ', s_color);
        }
    } else {
        s_buffer[s_row * VGA_WIDTH + s_col] = make_entry(c, s_color);
        if (++s_col >= VGA_WIDTH) {
            s_col = 0;
            if (++s_row >= VGA_HEIGHT) {
                scroll();
            }
        }
    }
    update_cursor();
}

void VgaConsole::write(const char* str) {
    if (!str) return;
    while (*str) {
        put_char(*str++);
    }
}

void VgaConsole::write(const char* str, size_t length) {
    if (!str) return;
    for (size_t i = 0; i < length; ++i) {
        put_char(str[i]);
    }
}

void VgaConsole::update_cursor() {
    uint16_t pos = static_cast<uint16_t>(s_row * VGA_WIDTH + s_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, static_cast<uint8_t>(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, static_cast<uint8_t>((pos >> 8) & 0xFF));
}

void VgaConsole::set_cursor(size_t col, size_t row) {
    if (col < VGA_WIDTH) s_col = col;
    if (row < VGA_HEIGHT) s_row = row;
    update_cursor();
}

} // namespace llamaos::drivers
