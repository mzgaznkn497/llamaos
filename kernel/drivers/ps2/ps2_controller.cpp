#include "ps2_controller.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - PS/2 Controller Implementation
// =============================================================================

namespace llamaos::drivers {

using namespace arch::x86_64;

bool Ps2Controller::s_initialized{false};
bool Ps2Controller::s_second_channel{false};
uint8_t Ps2Controller::s_config_byte{0};

uint8_t Ps2Controller::read_status() {
    return inb(PORT_STATUS);
}

uint8_t Ps2Controller::read_data() {
    return inb(PORT_DATA);
}

bool Ps2Controller::wait_input_clear(uint32_t timeout_cycles) {
    for (uint32_t i = 0; i < timeout_cycles; ++i) {
        if ((read_status() & STATUS_INPUT_FULL) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

bool Ps2Controller::wait_output_full(uint32_t timeout_cycles) {
    for (uint32_t i = 0; i < timeout_cycles; ++i) {
        if ((read_status() & STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

bool Ps2Controller::send_command(uint8_t command) {
    if (!wait_input_clear()) {
        return false;
    }
    outb(PORT_COMMAND, command);
    return true;
}

bool Ps2Controller::send_data(uint8_t data) {
    if (!wait_input_clear()) {
        return false;
    }
    outb(PORT_DATA, data);
    return true;
}

void Ps2Controller::flush_output_buffer() {
    for (size_t i = 0; i < 64; ++i) {
        if ((read_status() & STATUS_OUTPUT_FULL) != 0) {
            (void)read_data();
            io_wait();
        } else {
            break;
        }
    }
}

bool Ps2Controller::init() {
    klog_info("Initializing Intel 8042 PS/2 Controller...");

    // Check if status port is responsive (0xFF floating bus indicates absent controller)
    uint8_t initial_status = read_status();
    if (initial_status == 0xFF) {
        klog_warn("PS/2 Controller not present (status reads 0xFF).");
        return false;
    }

    // Step 1: Disable both devices
    send_command(CMD_DISABLE_PORT1);
    send_command(CMD_DISABLE_PORT2);

    // Step 2: Flush remaining bytes in output buffer
    flush_output_buffer();

    // Step 3: Inspect & configure controller configuration byte
    if (!send_command(CMD_READ_CONFIG) || !wait_output_full()) {
        klog_warn("PS/2 Controller: Failed to read configuration byte.");
        return false;
    }
    s_config_byte = read_data();

    // Modify configuration:
    // bit 0 = 1: Enable First Port IRQ1
    // bit 1 = 0: Disable Second Port IRQ12 for now
    // bit 4 = 0: Enable First Port clock
    // bit 6 = 1: Enable Translation (forces Set 2 -> Set 1 translation on port 0x60)
    uint8_t new_config = s_config_byte;
    new_config |= (1 << 0);  // Enable IRQ1
    new_config &= ~(1 << 1); // Disable IRQ12
    new_config &= ~(1 << 4); // Enable First Port Clock
    new_config |= (1 << 6);  // Enable Translation

    if (!send_command(CMD_WRITE_CONFIG) || !send_data(new_config)) {
        klog_warn("PS/2 Controller: Failed to write configuration byte.");
        return false;
    }
    s_config_byte = new_config;

    // Step 4: Perform Controller Self-Test
    if (!send_command(CMD_TEST_CONTROLLER) || !wait_output_full()) {
        klog_warn("PS/2 Controller: Self-test command timed out.");
        return false;
    }
    uint8_t self_test_res = read_data();
    if (self_test_res != 0x55) {
        klog_warn("PS/2 Controller: Self-test failed with code 0x%02x (expected 0x55).", self_test_res);
        return false;
    }

    // Rewrite configuration byte as self-test can reset controller on some hardware
    send_command(CMD_WRITE_CONFIG);
    send_data(new_config);

    // Step 5: Test First Port interface
    if (!send_command(CMD_TEST_PORT1) || !wait_output_full()) {
        klog_warn("PS/2 Controller: First port interface test timed out.");
    } else {
        uint8_t port1_res = read_data();
        if (port1_res != 0x00) {
            klog_warn("PS/2 Controller: Port 1 interface test returned 0x%02x (expected 0x00).", port1_res);
        }
    }

    // Step 6: Detect second channel
    s_second_channel = false;
    if (send_command(CMD_ENABLE_PORT2)) {
        if (send_command(CMD_READ_CONFIG) && wait_output_full()) {
            uint8_t cfg2 = read_data();
            // If clock disabled bit (bit 5) is cleared, port 2 is available
            if ((cfg2 & (1 << 5)) == 0) {
                s_second_channel = true;
            }
        }
        send_command(CMD_DISABLE_PORT2); // Keep disabled until mouse driver milestone
    }

    // Step 7: Enable First Port (Keyboard)
    if (!send_command(CMD_ENABLE_PORT1)) {
        klog_warn("PS/2 Controller: Failed to enable first port.");
        return false;
    }

    s_initialized = true;
    klog_info("PS/2 Controller Initialized successfully (Config=0x%02x, DualChannel=%s).",
              s_config_byte, s_second_channel ? "true" : "false");
    return true;
}

} // namespace llamaos::drivers
