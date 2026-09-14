#include "timer.hpp"
#include "pic.hpp"
#include "io.hpp"
#include "cpu.hpp"
#include "interrupts.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Timer Interrupt Foundation Implementation
// =============================================================================

namespace llamaos::arch::x86_64 {

volatile uint64_t g_timer_ticks{0};
uint32_t Timer::s_frequency{DEFAULT_HZ};
bool Timer::s_initialized{false};
Timer::TickHook Timer::s_tick_hook{nullptr};

void Timer::set_tick_hook(TickHook hook) noexcept {
    s_tick_hook = hook;
}

void Timer::dispatch_tick() noexcept {
    if (s_tick_hook) {
        s_tick_hook();
    }
}

void handle_timer_interrupt() {
    g_timer_ticks = g_timer_ticks + 1;
    PicManager::send_eoi(0);
    Timer::dispatch_tick();
}

void Timer::init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = DEFAULT_HZ;
    s_frequency = frequency_hz;

    // Divisor calculation using rounding to nearest integer: (base + (freq / 2)) / freq
    uint32_t divisor = (PIT_BASE_FREQ_HZ + (frequency_hz / 2)) / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    // Command byte: 0x34
    //   bit 7..6 = 00  (Channel 0)
    //   bit 5..4 = 11  (Access mode: lobyte/hibyte)
    //   bit 3..1 = 010 (Operating Mode 2: Rate Generator)
    //   bit 0    = 0   (16-bit binary mode)
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0_DATA, static_cast<uint8_t>(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, static_cast<uint8_t>((divisor >> 8) & 0xFF));

    // Unmask IRQ0 on the PIC
    PicManager::unmask_irq(0);

    s_initialized = true;
    klog_info("PIT Timer Initialized: Target=%u Hz, Divisor=%u (0x%04x), Command=0x34 (Mode 2 Rate Generator), IRQ0 unmasked.",
              s_frequency, divisor, divisor);
}

uint64_t Timer::ticks() noexcept {
    return g_timer_ticks;
}

bool Timer::verify_interrupt_api() {
    // 1. Initial state: Interrupts must be disabled
    if (interrupts_enabled()) {
        klog_error("Interrupt API Verify Failed: Interrupts unexpectedly enabled at test entry!");
        return false;
    }

    // 2. Enable interrupts: IF must become set
    enable_interrupts();
    if (!interrupts_enabled()) {
        klog_error("Interrupt API Verify Failed: enable_interrupts() failed to set IF!");
        return false;
    }

    // 3. Save and disable: IF must become clear, saved flags must have IF
    uint64_t saved = save_and_disable_interrupts();
    if (interrupts_enabled()) {
        klog_error("Interrupt API Verify Failed: save_and_disable_interrupts() failed to clear IF!");
        return false;
    }
    if ((saved & (1ULL << 9)) == 0) {
        klog_error("Interrupt API Verify Failed: Saved flags missing IF bit!");
        return false;
    }

    // 4. Restore state: IF must be restored to enabled
    restore_interrupt_state(saved);
    if (!interrupts_enabled()) {
        klog_error("Interrupt API Verify Failed: restore_interrupt_state() failed to restore IF!");
        return false;
    }

    // 5. Final disable
    disable_interrupts();
    if (interrupts_enabled()) {
        klog_error("Interrupt API Verify Failed: disable_interrupts() failed to clear IF!");
        return false;
    }

    klog_info(" [PASS] Architecture Interrupt Control API (STI/CLI/Save/Restore confirmed)");
    return true;
}

bool Timer::verify() {
    if (!s_initialized) {
        klog_error("Timer Verify Failed: Timer subsystem not initialized!");
        return false;
    }

    klog_info("Verifying Timer Interrupt delivery and periodic heartbeat...");
    uint64_t ticks_before = g_timer_ticks;

    // Enable interrupts to allow IRQ0 timer delivery
    enable_interrupts();

    // Bounded wait: wait for at least 3 timer ticks or timeout after bounded CPU cycles
    uint64_t start_tsc = rdtsc();
    // 1.5 billion cycles is ~0.5 to 1.5 seconds on modern x86 hardware, ample for 100Hz (10ms) ticks
    uint64_t timeout_cycles = 1500000000ULL;

    while ((g_timer_ticks - ticks_before) < 3 && (rdtsc() - start_tsc) < timeout_cycles) {
        halt(); // Wait in low-power state for timer interrupt
    }

    uint64_t ticks_after = g_timer_ticks;

    // Disable interrupts to keep deterministic control
    disable_interrupts();

    if (ticks_after <= ticks_before) {
        klog_error("Timer Verify Failed: No timer ticks received! (before=%llu, after=%llu)",
                   ticks_before, ticks_after);
        return false;
    }

    klog_info(" [PASS] Timer Interrupt Foundation & Periodic Heartbeat (%llu ticks received, tick=%llu)",
              ticks_after - ticks_before, ticks_after);
    return true;
}

} // namespace llamaos::arch::x86_64
