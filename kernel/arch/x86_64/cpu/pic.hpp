#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Programmable Interrupt Controller (Dual 8259 PIC & APIC Foundation)
// =============================================================================
// Provides APIC hardware detection, dual 8259 PIC remapping (Master: 0x20, Slave: 0x28),
// interrupt line masking/unmasking, End-of-Interrupt (EOI) signaling, and
// spurious interrupt discrimination for IRQ7 and IRQ15.
// =============================================================================

namespace llamaos::arch::x86_64 {

class PicManager {
public:
    static constexpr uint16_t PIC1_COMMAND = 0x20;
    static constexpr uint16_t PIC1_DATA    = 0x21;
    static constexpr uint16_t PIC2_COMMAND = 0xA0;
    static constexpr uint16_t PIC2_DATA    = 0xA1;

    static constexpr uint8_t  PIC_EOI      = 0x20;
    static constexpr uint8_t  MASTER_OFFSET= 0x20; // Vectors 32 .. 39
    static constexpr uint8_t  SLAVE_OFFSET = 0x28; // Vectors 40 .. 47

    // Detects APIC/PIC hardware state, remaps PIC vectors, and establishes mask
    static void init();

    // Verifies PIC configuration and hardware responsiveness
    static bool verify();

    // Sends End-of-Interrupt (EOI) signal for the given IRQ
    static void send_eoi(uint8_t irq);

    // Masks or unmasks a specific IRQ line (0 .. 15)
    static void mask_irq(uint8_t irq);
    static void unmask_irq(uint8_t irq);

    // Reads current In-Service Register (ISR) or Interrupt Request Register (IRR)
    static uint16_t read_isr();
    static uint16_t read_irr();

    // Spurious interrupt telemetry
    static uint64_t spurious_irq7_count() noexcept { return s_spurious_irq7; }
    static void record_spurious_irq7() noexcept { s_spurious_irq7 = s_spurious_irq7 + 1; }
    static void record_spurious_irq15() noexcept { s_spurious_irq15 = s_spurious_irq15 + 1; }

    // APIC Detection results
    static bool apic_supported() noexcept { return s_apic_supported; }
    static bool apic_enabled() noexcept { return s_apic_enabled; }
    static uint64_t apic_base_physical() noexcept { return s_apic_base_paddr; }

private:
    static bool s_initialized;
    static bool s_apic_supported;
    static bool s_apic_enabled;
    static uint64_t s_apic_base_paddr;

    static volatile uint64_t s_spurious_irq7;
    static volatile uint64_t s_spurious_irq15;
};

} // namespace llamaos::arch::x86_64
