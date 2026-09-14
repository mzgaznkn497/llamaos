#include "keyboard.hpp"
#include "ps2_controller.hpp"
#include "arch/x86_64/cpu/pic.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - PS/2 Keyboard Driver Implementation
// =============================================================================

namespace llamaos::drivers {

using namespace arch::x86_64;

bool Keyboard::s_initialized{false};
KeyboardEventQueue Keyboard::s_queue{};
ScancodeDecoder Keyboard::s_decoder{};
volatile uint64_t Keyboard::s_scancodes_received{0};
volatile uint64_t Keyboard::s_events_generated{0};

bool Keyboard::init() {
    klog_info("Initializing PS/2 Keyboard Driver...");

    if (!Ps2Controller::is_initialized()) {
        if (!Ps2Controller::init()) {
            klog_warn("Keyboard init: PS/2 controller initialization failed.");
            return false;
        }
    }

    // Flush any leftover scancodes
    Ps2Controller::flush_output_buffer();

    // Reset decoder and queue
    s_decoder.reset();
    s_queue.clear();
    s_scancodes_received = 0;
    s_events_generated = 0;

    // Send Enable Scanning command (0xF4) to keyboard
    if (Ps2Controller::send_data(0xF4)) {
        if (Ps2Controller::wait_output_full()) {
            uint8_t ack = Ps2Controller::read_data();
            (void)ack; // 0xFA ACK
        }
    }

    // Unmask IRQ1 on the Master PIC (Vector 33)
    PicManager::unmask_irq(1);

    s_initialized = true;
    klog_info("PS/2 Keyboard Driver Initialized (IRQ1 unmasked, Vector 33 active).");
    return true;
}

bool Keyboard::process_scancode(uint8_t scancode) noexcept {
    s_scancodes_received = s_scancodes_received + 1;

    KeyEvent ev{};
    if (s_decoder.process_byte(scancode, ev)) {
        s_events_generated = s_events_generated + 1;
        return s_queue.push(ev);
    }
    return false;
}

void Keyboard::handle_interrupt() noexcept {
    // 1. Read controller status
    const uint8_t status = inb(Ps2Controller::PORT_STATUS);

    // 2. If data is ready and not from auxiliary mouse device, read and process
    if ((status & Ps2Controller::STATUS_OUTPUT_FULL) != 0) {
        const uint8_t scancode = inb(Ps2Controller::PORT_DATA);
        // Only process keyboard data (bit 5 == 0)
        if ((status & Ps2Controller::STATUS_AUX_OUTPUT) == 0) {
            process_scancode(scancode);
        }
    }

    // 3. Send End of Interrupt (EOI) to Master PIC for IRQ1
    PicManager::send_eoi(1);
}

bool Keyboard::pop_event(KeyEvent* out_event) noexcept {
    return s_queue.pop(out_event);
}

size_t Keyboard::available() noexcept {
    return s_queue.count();
}

uint64_t Keyboard::dropped_count() noexcept {
    return s_queue.dropped_count();
}

KeyModifiers Keyboard::modifiers() noexcept {
    return s_decoder.current_modifiers();
}

} // namespace llamaos::drivers

// Low-level C ISR hook called by assembly exception dispatcher (vector 33)
extern "C" void handle_keyboard_interrupt() {
    llamaos::drivers::Keyboard::handle_interrupt();
}
