#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - 16550 UART Serial Driver
// =============================================================================
// Provides serial console communication on standard COM1 (0x3F8) port, plus
// mirrored output to hypervisor debug port 0xE9 for QEMU/Bochs diagnostics.
// =============================================================================

namespace llamaos::drivers {

class SerialPort {
public:
    static constexpr uint16_t COM1_BASE = 0x03F8;
    static constexpr uint16_t COM2_BASE = 0x02F8;
    static constexpr uint16_t DEBUG_PORT = 0x00E9; // Bochs/QEMU direct port

    static bool init(uint16_t base = COM1_BASE);
    static void put_char(char c);
    static void write(const char* str);
    static void write(const char* str, size_t length);
    static void write_hex(uint64_t value, uint8_t width = 16);
    static void write_dec(uint64_t value);
    static void write_signed_dec(int64_t value);

    static bool is_transmit_empty(uint16_t base = COM1_BASE);
    static bool has_rx(uint16_t base = COM1_BASE);
    static char get_char(uint16_t base = COM1_BASE);

private:
    static inline uint16_t s_base_port = COM1_BASE;
    static inline bool s_initialized = false;
};

} // namespace llamaos::drivers
