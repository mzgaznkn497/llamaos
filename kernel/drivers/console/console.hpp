#pragma once

#include "core/types.hpp"
#include "drivers/vga.hpp"
#include "drivers/serial.hpp"

// =============================================================================
// LlamaOS/A - Unified Kernel Console Interface
// =============================================================================
// Provides a centralized, strongly-typed console abstraction over both the
// 16550 UART serial port (for headless debugging and automated test harnesses)
// and the standard VGA text-mode buffer (for local visual interaction).
// =============================================================================

namespace llamaos::drivers {

class Console {
public:
    // Initializes the console layer (serial port COM1 and VGA text buffer)
    static void init();

    // Clears the visual screen and homes the cursor
    static void clear();

    // Outputs a single character to all active console backends
    static void put_char(char c);

    // Outputs a null-terminated string
    static void write(const char* str);

    // Outputs a string of specified length
    static void write(const char* str, size_t length);

    // Outputs a string followed by a newline
    static void write_line(const char* str);

    // Sets foreground and background colors for VGA console
    static void set_color(VgaColor fg, VgaColor bg);

    // Updates visual cursor position
    static void set_cursor(size_t col, size_t row);

    // Queries current cursor position
    static void get_cursor(size_t& out_col, size_t& out_row);

    // Returns whether visual VGA text console is active
    static bool is_vga_active() noexcept { return s_vga_active; }

    // Returns whether serial console is active
    static bool is_serial_active() noexcept { return s_serial_active; }

private:
    static bool s_initialized;
    static bool s_vga_active;
    static bool s_serial_active;
};

} // namespace llamaos::drivers
