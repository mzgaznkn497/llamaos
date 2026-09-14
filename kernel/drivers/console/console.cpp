#include "console.hpp"

// =============================================================================
// LlamaOS/A - Unified Kernel Console Implementation
// =============================================================================

namespace llamaos::drivers {

bool Console::s_initialized{false};
bool Console::s_vga_active{false};
bool Console::s_serial_active{false};

void Console::init() {
    SerialPort::init(SerialPort::COM1_BASE);
    s_serial_active = true;

    VgaConsole::init();
    s_vga_active = true;

    s_initialized = true;
}

void Console::clear() {
    if (s_vga_active) {
        VgaConsole::clear();
    }
}

void Console::put_char(char c) {
    if (s_serial_active) {
        SerialPort::put_char(c);
    }
    if (s_vga_active) {
        VgaConsole::put_char(c);
    }
}

void Console::write(const char* str) {
    if (!str) return;
    while (*str) {
        put_char(*str++);
    }
}

void Console::write(const char* str, size_t length) {
    if (!str) return;
    for (size_t i = 0; i < length; ++i) {
        put_char(str[i]);
    }
}

void Console::write_line(const char* str) {
    write(str);
    put_char('\n');
}

void Console::set_color(VgaColor fg, VgaColor bg) {
    if (s_vga_active) {
        VgaConsole::set_color(fg, bg);
    }
}

void Console::set_cursor(size_t col, size_t row) {
    if (s_vga_active) {
        VgaConsole::set_cursor(col, row);
    }
}

void Console::get_cursor(size_t& out_col, size_t& out_row) {
    if (s_vga_active) {
        VgaConsole::get_cursor(out_col, out_row);
    } else {
        out_col = 0;
        out_row = 0;
    }
}

} // namespace llamaos::drivers
