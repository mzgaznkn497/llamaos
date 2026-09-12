#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - x86-64 Low-Level Port I/O Helpers
// =============================================================================
// Provides type-safe freestanding assembly wrappers for x86-64 I/O port instructions.
// =============================================================================

namespace llamaos::arch::x86_64 {

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void io_wait() {
    // Port 0x80 is traditionally used for POST checkpoint codes and unused by devices,
    // producing a reliable ~1-2 microsecond delay on legacy bus operations.
    outb(0x80, 0x00);
}

} // namespace llamaos::arch::x86_64
