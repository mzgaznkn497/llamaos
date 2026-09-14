#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Interrupt Descriptor Table (IDT) Architecture
// =============================================================================
// Implements the 256-entry x86-64 Interrupt Descriptor Table (IDT) with strongly
// typed gate descriptors. Configures exception gates (#DE through #CP), hardware
// IRQ gates, and spurious interrupt gates with explicit kernel selectors and ISTs.
// =============================================================================

namespace llamaos::arch::x86_64 {

enum class GateType : uint8_t {
    InterruptGate = 0x0E, // 64-bit Interrupt Gate (Clears IF on entry)
    TrapGate      = 0x0F  // 64-bit Trap Gate (Preserves IF on entry)
};

// 16-byte x86-64 IDT Gate Descriptor
struct [[gnu::packed]] IdtEntry {
    uint16_t offset_low{0};
    uint16_t selector{0};
    uint8_t  ist : 3 {0};
    uint8_t  reserved0 : 5 {0};
    uint8_t  type : 4 {0};
    uint8_t  zero : 1 {0};
    uint8_t  dpl : 2 {0};
    uint8_t  present : 1 {0};
    uint16_t offset_mid{0};
    uint32_t offset_high{0};
    uint32_t reserved1{0};

    [[nodiscard]] uint64_t handler_address() const noexcept {
        return static_cast<uint64_t>(offset_low)
             | (static_cast<uint64_t>(offset_mid) << 16)
             | (static_cast<uint64_t>(offset_high) << 32);
    }
};

static_assert(sizeof(IdtEntry) == 16, "IdtEntry must be exactly 16 bytes");

// 10-byte IDTR register structure for LIDT / SIDT
struct [[gnu::packed]] Idtr {
    uint16_t limit;
    uint64_t base;
};

static_assert(sizeof(Idtr) == 10, "Idtr must be exactly 10 bytes");

// Helper function for gate encoding (used in kernel and host regression tests)
constexpr IdtEntry encode_idt_gate(uint64_t handler, uint16_t selector, GateType type, uint8_t dpl = 0, uint8_t ist = 0) {
    IdtEntry entry{};
    entry.offset_low = handler & 0xFFFFULL;
    entry.selector = selector;
    entry.ist = ist & 0x07;
    entry.reserved0 = 0;
    entry.type = static_cast<uint8_t>(type) & 0x0F;
    entry.zero = 0;
    entry.dpl = dpl & 0x03;
    entry.present = 1;
    entry.offset_mid = (handler >> 16) & 0xFFFFULL;
    entry.offset_high = (handler >> 32) & 0xFFFFFFFFULL;
    entry.reserved1 = 0;
    return entry;
}

class IdtManager {
public:
    static void init();
    static bool verify();

    static void install_gate(size_t vector, uint64_t handler, GateType type, uint8_t dpl = 0, uint8_t ist = 0);
    static const IdtEntry& get_gate(size_t vector) noexcept { return s_idt[vector]; }

private:
    alignas(4096) static IdtEntry s_idt[256];
};

inline void lidt(const Idtr& idtr) {
    asm volatile("lidt %0" : : "m"(idtr));
}

inline void sidt(Idtr& idtr) {
    asm volatile("sidt %0" : "=m"(idtr));
}

} // namespace llamaos::arch::x86_64
