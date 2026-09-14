#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Timer Interrupt Foundation (PIT 8254 & Periodic Heartbeat)
// =============================================================================
// Implements periodic timer interrupts using the Programmable Interval Timer (PIT),
// tracks monotonic system uptime ticks, and executes deterministic interrupt
// delivery verification.
// =============================================================================

namespace llamaos::arch::x86_64 {

class Timer {
public:
    static constexpr uint16_t PIT_CHANNEL0_DATA = 0x40;
    static constexpr uint16_t PIT_COMMAND       = 0x43;
    static constexpr uint32_t PIT_BASE_FREQ_HZ  = 1193182;
    static constexpr uint32_t DEFAULT_HZ        = 100; // 100 Hz = 10 ms per tick

    // Configures PIT Channel 0 for periodic mode and unmasks IRQ0 on the PIC
    static void init(uint32_t frequency_hz = DEFAULT_HZ);

    // Bounded hardware verification requiring ticks_after > ticks_before
    static bool verify();

    // Monotonic tick count since timer initialization
    static uint64_t ticks() noexcept;

    // Interrupt control API verification
    static bool verify_interrupt_api();

    // Periodic timer interrupt callback hook (e.g. for scheduler preemption)
    using TickHook = void (*)(void);
    static void set_tick_hook(TickHook hook) noexcept;
    static void dispatch_tick() noexcept;

    friend void handle_timer_interrupt();

private:
    static uint32_t s_frequency;
    static bool s_initialized;
    static TickHook s_tick_hook;
};

// Global tick counter incremented by timer ISR
extern volatile uint64_t g_timer_ticks;

} // namespace llamaos::arch::x86_64
