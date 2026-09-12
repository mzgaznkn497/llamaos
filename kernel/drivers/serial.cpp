#include "serial.hpp"
#include "arch/x86_64/cpu/io.hpp"

// =============================================================================
// LlamaOS/A - 16550 UART Serial Driver Implementation
// =============================================================================

namespace llamaos::drivers {

using namespace arch::x86_64;

bool SerialPort::init(uint16_t base) {
    s_base_port = base;

    // 1. Disable all UART interrupts
    outb(base + 1, 0x00);

    // 2. Enable DLAB (Divisor Latch Access Bit) to set baud rate
    outb(base + 3, 0x80);

    // 3. Set divisor to 1 (115200 baud)
    outb(base + 0, 0x01); // Divisor low byte
    outb(base + 1, 0x00); // Divisor high byte

    // 4. Configure line control: 8 bits, no parity, 1 stop bit (8-N-1)
    outb(base + 3, 0x03);

    // 5. Enable FIFO, clear TX/RX queues, 14-byte interrupt threshold
    outb(base + 2, 0xC7);

    // 6. Modem control register: Enable auxiliary output 2, RTS, DTR
    outb(base + 4, 0x0B);

    // 7. Perform loopback test to ensure hardware responds
    outb(base + 4, 0x1E); // Set loopback mode
    outb(base + 0, 0xAE); // Transmit test byte
    uint8_t echo = inb(base + 0);

    // Set normal operational mode
    outb(base + 4, 0x0F);

    s_initialized = (echo == 0xAE);
    return s_initialized;
}

bool SerialPort::is_transmit_empty(uint16_t base) {
    return (inb(base + 5) & 0x20) != 0;
}

void SerialPort::put_char(char c) {
    // Always mirror to QEMU/Bochs debug port 0xE9
    outb(DEBUG_PORT, static_cast<uint8_t>(c));

    if (c == '\n') {
        while (!is_transmit_empty(s_base_port)) {}
        outb(s_base_port, '\r');
    }

    while (!is_transmit_empty(s_base_port)) {}
    outb(s_base_port, static_cast<uint8_t>(c));
}

void SerialPort::write(const char* str) {
    if (!str) return;
    while (*str) {
        put_char(*str++);
    }
}

void SerialPort::write(const char* str, size_t length) {
    if (!str) return;
    for (size_t i = 0; i < length; ++i) {
        put_char(str[i]);
    }
}

void SerialPort::write_hex(uint64_t value, uint8_t width) {
    static const char hex_chars[] = "0123456789ABCDEF";
    write("0x");
    if (width == 0 || width > 16) width = 16;
    for (int i = (width - 1) * 4; i >= 0; i -= 4) {
        uint8_t nibble = static_cast<uint8_t>((value >> i) & 0x0F);
        put_char(hex_chars[nibble]);
    }
}

void SerialPort::write_dec(uint64_t value) {
    if (value == 0) {
        put_char('0');
        return;
    }
    char buf[32];
    int idx = 0;
    while (value > 0) {
        buf[idx++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    for (int i = idx - 1; i >= 0; --i) {
        put_char(buf[i]);
    }
}

void SerialPort::write_signed_dec(int64_t value) {
    if (value < 0) {
        put_char('-');
        write_dec(static_cast<uint64_t>(-value));
    } else {
        write_dec(static_cast<uint64_t>(value));
    }
}

} // namespace llamaos::drivers
