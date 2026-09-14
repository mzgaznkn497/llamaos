#pragma once

#include "input_event.hpp"

// =============================================================================
// LlamaOS/A - PS/2 Keyboard Scancode Decoder (Set 1)
// =============================================================================
// Pure, freestanding, host-testable decoder converting raw PS/2 keyboard
// scancode byte streams (IBM PC Scan Code Set 1) into structured KeyEvents.
// Handles multi-byte prefixes (0xE0, 0xE1), make/break codes, modifier tracking
// (Shift, Ctrl, Alt, CapsLock), and recovery from malformed sequences.
// =============================================================================

namespace llamaos::drivers {

class ScancodeDecoder {
public:
    enum class State : uint8_t {
        Normal,
        PrefixE0,
        PrefixE1_1,
        PrefixE1_2
    };

    ScancodeDecoder() = default;

    // Resets decoder internal state machine and modifiers
    void reset() noexcept;

    // Feeds one raw scancode byte into the state machine.
    // Returns true if a complete KeyEvent was produced, false if more bytes needed or ignored.
    bool process_byte(uint8_t byte, KeyEvent& out_event);

    // Modifier state queries
    bool is_shift_down() const noexcept { return m_lshift || m_rshift; }
    bool is_ctrl_down() const noexcept { return m_lctrl || m_rctrl; }
    bool is_alt_down() const noexcept { return m_lalt || m_ralt; }
    bool is_caps_locked() const noexcept { return m_caps; }

    KeyModifiers current_modifiers() const noexcept {
        return KeyModifiers{
            .shift = is_shift_down(),
            .ctrl  = is_ctrl_down(),
            .alt   = is_alt_down(),
            .caps  = m_caps
        };
    }

private:
    State m_state{State::Normal};
    bool m_lshift{false};
    bool m_rshift{false};
    bool m_lctrl{false};
    bool m_rctrl{false};
    bool m_lalt{false};
    bool m_ralt{false};
    bool m_caps{false};

    static KeyCode map_set1_normal(uint8_t code) noexcept;
    static KeyCode map_set1_extended(uint8_t code) noexcept;
};

} // namespace llamaos::drivers
