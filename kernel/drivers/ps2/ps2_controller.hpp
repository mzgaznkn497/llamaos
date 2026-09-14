#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - PS/2 (Intel 8042) Controller Foundation
// =============================================================================
// Manages the dual-channel PS/2 keyboard/mouse controller through standard
// legacy ports 0x60 (Data) and 0x64 (Status / Command). Provides bounded wait
// primitives, command submission, buffer flushing, self-tests, and channel detection.
// =============================================================================

namespace llamaos::drivers {

class Ps2Controller {
public:
    static constexpr uint16_t PORT_DATA    = 0x60;
    static constexpr uint16_t PORT_COMMAND = 0x64;
    static constexpr uint16_t PORT_STATUS  = 0x64;

    // Status register bitfield flags
    static constexpr uint8_t STATUS_OUTPUT_FULL = (1 << 0); // 1 = Data ready to read
    static constexpr uint8_t STATUS_INPUT_FULL  = (1 << 1); // 1 = Controller busy
    static constexpr uint8_t STATUS_SYSTEM_FLAG = (1 << 2);
    static constexpr uint8_t STATUS_COMMAND_DATA= (1 << 3);
    static constexpr uint8_t STATUS_KEYBOARD_LOCK=(1 << 4);
    static constexpr uint8_t STATUS_AUX_OUTPUT  = (1 << 5); // 1 = Mouse data
    static constexpr uint8_t STATUS_TIMEOUT     = (1 << 6);
    static constexpr uint8_t STATUS_PARITY_ERROR= (1 << 7);

    // Controller Commands
    static constexpr uint8_t CMD_READ_CONFIG    = 0x20;
    static constexpr uint8_t CMD_WRITE_CONFIG   = 0x60;
    static constexpr uint8_t CMD_DISABLE_PORT2  = 0xA7;
    static constexpr uint8_t CMD_ENABLE_PORT2   = 0xA8;
    static constexpr uint8_t CMD_TEST_PORT2     = 0xA9;
    static constexpr uint8_t CMD_TEST_CONTROLLER= 0xAA;
    static constexpr uint8_t CMD_TEST_PORT1     = 0xAB;
    static constexpr uint8_t CMD_DISABLE_PORT1  = 0xAD;
    static constexpr uint8_t CMD_ENABLE_PORT1   = 0xAE;

    // Initializes and audits the PS/2 controller
    static bool init();

    // Bounded wait primitives
    static bool wait_input_clear(uint32_t timeout_cycles = 100000);
    static bool wait_output_full(uint32_t timeout_cycles = 100000);

    // Command submission & response reading
    static bool send_command(uint8_t command);
    static bool send_data(uint8_t data);
    static uint8_t read_data();
    static uint8_t read_status();

    // Flushes all pending output buffer bytes
    static void flush_output_buffer();

    // State queries
    static bool is_initialized() noexcept { return s_initialized; }
    static bool has_second_channel() noexcept { return s_second_channel; }
    static uint8_t config_byte() noexcept { return s_config_byte; }

private:
    static bool s_initialized;
    static bool s_second_channel;
    static uint8_t s_config_byte;
};

} // namespace llamaos::drivers
