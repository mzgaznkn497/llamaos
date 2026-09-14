#pragma once

#include "input_event.hpp"
#include "input_queue.hpp"
#include "scancode.hpp"

// =============================================================================
// LlamaOS/A - PS/2 Keyboard Driver
// =============================================================================
// Provides interrupt-driven keyboard input via IRQ1 (Vector 33 / 0x21).
// Feeds scancodes into the ScancodeDecoder, buffers KeyEvents in a bounded queue,
// and enforces minimal ISR discipline with zero heap allocation.
// =============================================================================

namespace llamaos::drivers {

class Keyboard {
public:
    // Initializes the keyboard driver, resets the device, and unmasks IRQ1
    static bool init();

    // Interrupt service routine entry point (invoked from vector 33)
    static void handle_interrupt() noexcept;

    // Direct scancode feeding (used by ISR and test harnesses)
    static bool process_scancode(uint8_t scancode) noexcept;

    // Kernel consumer: retrieves the oldest KeyEvent from the queue
    static bool pop_event(KeyEvent* out_event) noexcept;

    // Telemetry and capacity queries
    static size_t available() noexcept;
    static uint64_t dropped_count() noexcept;
    static uint64_t total_scancodes_received() noexcept { return s_scancodes_received; }
    static uint64_t total_events_generated() noexcept { return s_events_generated; }
    static bool is_initialized() noexcept { return s_initialized; }

    // Access to current modifier state
    static KeyModifiers modifiers() noexcept;

private:
    static bool s_initialized;
    static KeyboardEventQueue s_queue;
    static ScancodeDecoder s_decoder;
    static volatile uint64_t s_scancodes_received;
    static volatile uint64_t s_events_generated;
};

// Low-level C interrupt binding declared for exception dispatcher
extern "C" void handle_keyboard_interrupt();

} // namespace llamaos::drivers
