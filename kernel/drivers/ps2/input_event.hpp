#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Input Event Abstraction
// =============================================================================
// Strongly-typed representation of keyboard input events, key actions,
// modifier states, and ASCII character translation.
// =============================================================================

namespace llamaos::drivers {

enum class KeyCode : uint16_t {
    Unknown = 0,

    // Alphanumeric keys
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Control & Editing keys
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,

    // Modifier keys
    CapsLock,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,

    // Navigation & Editing
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

enum class KeyAction : uint8_t {
    Press,
    Release
};

struct KeyModifiers {
    bool shift : 1;
    bool ctrl  : 1;
    bool alt   : 1;
    bool caps  : 1;
};

struct KeyEvent {
    KeyCode key{KeyCode::Unknown};
    KeyAction action{KeyAction::Press};
    KeyModifiers modifiers{false, false, false, false};
    uint8_t raw_scancode{0};
};

// Translates a structured KeyEvent into its corresponding ASCII character,
// accounting for Shift and CapsLock state. Returns '\0' for non-printable keys.
inline char key_event_to_ascii(const KeyEvent& ev) {
    if (ev.action != KeyAction::Press) {
        return '\0';
    }

    const bool shift = ev.modifiers.shift;
    const bool caps = ev.modifiers.caps;

    // Alphanumeric keys: Shift XOR Caps determines uppercase
    const bool upper = shift ^ caps;

    switch (ev.key) {
        case KeyCode::A: return upper ? 'A' : 'a';
        case KeyCode::B: return upper ? 'B' : 'b';
        case KeyCode::C: return upper ? 'C' : 'c';
        case KeyCode::D: return upper ? 'D' : 'd';
        case KeyCode::E: return upper ? 'E' : 'e';
        case KeyCode::F: return upper ? 'F' : 'f';
        case KeyCode::G: return upper ? 'G' : 'g';
        case KeyCode::H: return upper ? 'H' : 'h';
        case KeyCode::I: return upper ? 'I' : 'i';
        case KeyCode::J: return upper ? 'J' : 'j';
        case KeyCode::K: return upper ? 'K' : 'k';
        case KeyCode::L: return upper ? 'L' : 'l';
        case KeyCode::M: return upper ? 'M' : 'm';
        case KeyCode::N: return upper ? 'N' : 'n';
        case KeyCode::O: return upper ? 'O' : 'o';
        case KeyCode::P: return upper ? 'P' : 'p';
        case KeyCode::Q: return upper ? 'Q' : 'q';
        case KeyCode::R: return upper ? 'R' : 'r';
        case KeyCode::S: return upper ? 'S' : 's';
        case KeyCode::T: return upper ? 'T' : 't';
        case KeyCode::U: return upper ? 'U' : 'u';
        case KeyCode::V: return upper ? 'V' : 'v';
        case KeyCode::W: return upper ? 'W' : 'w';
        case KeyCode::X: return upper ? 'X' : 'x';
        case KeyCode::Y: return upper ? 'Y' : 'y';
        case KeyCode::Z: return upper ? 'Z' : 'z';

        case KeyCode::Num0: return shift ? ')' : '0';
        case KeyCode::Num1: return shift ? '!' : '1';
        case KeyCode::Num2: return shift ? '@' : '2';
        case KeyCode::Num3: return shift ? '#' : '3';
        case KeyCode::Num4: return shift ? '$' : '4';
        case KeyCode::Num5: return shift ? '%' : '5';
        case KeyCode::Num6: return shift ? '^' : '6';
        case KeyCode::Num7: return shift ? '&' : '7';
        case KeyCode::Num8: return shift ? '*' : '8';
        case KeyCode::Num9: return shift ? '(' : '9';

        case KeyCode::Enter:     return '\n';
        case KeyCode::Backspace: return '\b';
        case KeyCode::Tab:       return '\t';
        case KeyCode::Space:     return ' ';
        case KeyCode::Minus:        return shift ? '_' : '-';
        case KeyCode::Equals:       return shift ? '+' : '=';
        case KeyCode::LeftBracket:  return shift ? '{' : '[';
        case KeyCode::RightBracket: return shift ? '}' : ']';
        case KeyCode::Backslash:    return shift ? '|' : '\\';
        case KeyCode::Semicolon:    return shift ? ':' : ';';
        case KeyCode::Apostrophe:   return shift ? '"' : '\'';
        case KeyCode::Grave:        return shift ? '~' : '`';
        case KeyCode::Comma:        return shift ? '<' : ',';
        case KeyCode::Period:       return shift ? '>' : '.';
        case KeyCode::Slash:        return shift ? '?' : '/';

        default:
            return '\0';
    }
}

} // namespace llamaos::drivers
