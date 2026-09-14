#include "scancode.hpp"

// =============================================================================
// LlamaOS/A - PS/2 Keyboard Scancode Decoder Implementation
// =============================================================================

namespace llamaos::drivers {

void ScancodeDecoder::reset() noexcept {
    m_state = State::Normal;
    m_lshift = false;
    m_rshift = false;
    m_lctrl = false;
    m_rctrl = false;
    m_lalt = false;
    m_ralt = false;
    m_caps = false;
}

KeyCode ScancodeDecoder::map_set1_normal(uint8_t code) noexcept {
    switch (code) {
        case 0x01: return KeyCode::Escape;
        case 0x02: return KeyCode::Num1;
        case 0x03: return KeyCode::Num2;
        case 0x04: return KeyCode::Num3;
        case 0x05: return KeyCode::Num4;
        case 0x06: return KeyCode::Num5;
        case 0x07: return KeyCode::Num6;
        case 0x08: return KeyCode::Num7;
        case 0x09: return KeyCode::Num8;
        case 0x0A: return KeyCode::Num9;
        case 0x0B: return KeyCode::Num0;
        case 0x0C: return KeyCode::Minus;
        case 0x0D: return KeyCode::Equals;
        case 0x0E: return KeyCode::Backspace;
        case 0x0F: return KeyCode::Tab;
        case 0x10: return KeyCode::Q;
        case 0x11: return KeyCode::W;
        case 0x12: return KeyCode::E;
        case 0x13: return KeyCode::R;
        case 0x14: return KeyCode::T;
        case 0x15: return KeyCode::Y;
        case 0x16: return KeyCode::U;
        case 0x17: return KeyCode::I;
        case 0x18: return KeyCode::O;
        case 0x19: return KeyCode::P;
        case 0x1A: return KeyCode::LeftBracket;
        case 0x1B: return KeyCode::RightBracket;
        case 0x1C: return KeyCode::Enter;
        case 0x1D: return KeyCode::LeftCtrl;
        case 0x1E: return KeyCode::A;
        case 0x1F: return KeyCode::S;
        case 0x20: return KeyCode::D;
        case 0x21: return KeyCode::F;
        case 0x22: return KeyCode::G;
        case 0x23: return KeyCode::H;
        case 0x24: return KeyCode::J;
        case 0x25: return KeyCode::K;
        case 0x26: return KeyCode::L;
        case 0x27: return KeyCode::Semicolon;
        case 0x28: return KeyCode::Apostrophe;
        case 0x29: return KeyCode::Grave;
        case 0x2A: return KeyCode::LeftShift;
        case 0x2B: return KeyCode::Backslash;
        case 0x2C: return KeyCode::Z;
        case 0x2D: return KeyCode::X;
        case 0x2E: return KeyCode::C;
        case 0x2F: return KeyCode::V;
        case 0x30: return KeyCode::B;
        case 0x31: return KeyCode::N;
        case 0x32: return KeyCode::M;
        case 0x33: return KeyCode::Comma;
        case 0x34: return KeyCode::Period;
        case 0x35: return KeyCode::Slash;
        case 0x36: return KeyCode::RightShift;
        case 0x38: return KeyCode::LeftAlt;
        case 0x39: return KeyCode::Space;
        case 0x3A: return KeyCode::CapsLock;
        case 0x3B: return KeyCode::F1;
        case 0x3C: return KeyCode::F2;
        case 0x3D: return KeyCode::F3;
        case 0x3E: return KeyCode::F4;
        case 0x3F: return KeyCode::F5;
        case 0x40: return KeyCode::F6;
        case 0x41: return KeyCode::F7;
        case 0x42: return KeyCode::F8;
        case 0x43: return KeyCode::F9;
        case 0x44: return KeyCode::F10;
        case 0x57: return KeyCode::F11;
        case 0x58: return KeyCode::F12;
        default:   return KeyCode::Unknown;
    }
}

KeyCode ScancodeDecoder::map_set1_extended(uint8_t code) noexcept {
    switch (code) {
        case 0x1C: return KeyCode::Enter;     // Keypad Enter
        case 0x1D: return KeyCode::RightCtrl; // Right Control
        case 0x35: return KeyCode::Slash;     // Keypad Slash
        case 0x38: return KeyCode::RightAlt;  // Right Alt (AltGr)
        case 0x47: return KeyCode::Home;
        case 0x48: return KeyCode::ArrowUp;
        case 0x49: return KeyCode::PageUp;
        case 0x4B: return KeyCode::ArrowLeft;
        case 0x4D: return KeyCode::ArrowRight;
        case 0x4F: return KeyCode::End;
        case 0x50: return KeyCode::ArrowDown;
        case 0x51: return KeyCode::PageDown;
        case 0x52: return KeyCode::Insert;
        case 0x53: return KeyCode::Delete;
        default:   return KeyCode::Unknown;
    }
}

bool ScancodeDecoder::process_byte(uint8_t byte, KeyEvent& out_event) {
    if (m_state == State::Normal) {
        if (byte == 0xE0) {
            m_state = State::PrefixE0;
            return false;
        }
        if (byte == 0xE1) {
            m_state = State::PrefixE1_1;
            return false;
        }

        const bool is_release = (byte & 0x80) != 0;
        const uint8_t base_code = byte & 0x7F;
        const KeyCode key = map_set1_normal(base_code);

        // Update internal modifier tracking
        if (key == KeyCode::LeftShift) {
            m_lshift = !is_release;
        } else if (key == KeyCode::RightShift) {
            m_rshift = !is_release;
        } else if (key == KeyCode::LeftCtrl) {
            m_lctrl = !is_release;
        } else if (key == KeyCode::LeftAlt) {
            m_lalt = !is_release;
        } else if (key == KeyCode::CapsLock) {
            if (!is_release) {
                m_caps = !m_caps;
            }
        }

        out_event.key = key;
        out_event.action = is_release ? KeyAction::Release : KeyAction::Press;
        out_event.modifiers = current_modifiers();
        out_event.raw_scancode = byte;
        return true;
    }

    if (m_state == State::PrefixE0) {
        m_state = State::Normal;

        const bool is_release = (byte & 0x80) != 0;
        const uint8_t base_code = byte & 0x7F;
        const KeyCode key = map_set1_extended(base_code);

        // Update extended modifiers
        if (key == KeyCode::RightCtrl) {
            m_rctrl = !is_release;
        } else if (key == KeyCode::RightAlt) {
            m_ralt = !is_release;
        }

        out_event.key = key;
        out_event.action = is_release ? KeyAction::Release : KeyAction::Press;
        out_event.modifiers = current_modifiers();
        out_event.raw_scancode = byte;
        return true;
    }

    if (m_state == State::PrefixE1_1) {
        // Pause/Break sequence byte 2 (e.g. 0x1D)
        m_state = State::PrefixE1_2;
        return false;
    }

    if (m_state == State::PrefixE1_2) {
        // Pause/Break sequence byte 3 (e.g. 0x45) -> completed, return to normal
        m_state = State::Normal;
        return false;
    }

    m_state = State::Normal;
    return false;
}

} // namespace llamaos::drivers
