#pragma once

#include "core/types.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "arch/x86_64/cpu/interrupts.hpp"

// =============================================================================
// LlamaOS/A - Spinlock & Synchronization Primitives
// =============================================================================
// Provides interrupt-safe mutual exclusion spinlocks using atomic test-and-set
// and RAII lock guards for critical sections.
// =============================================================================

namespace llamaos::sync {

class Spinlock {
public:
    constexpr Spinlock() noexcept : m_locked(0) {}

    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;

    void lock() noexcept {
        while (__atomic_test_and_set(&m_locked, __ATOMIC_ACQUIRE)) {
            while (__atomic_load_n(&m_locked, __ATOMIC_RELAXED)) {
                arch::x86_64::pause();
            }
        }
    }

    bool try_lock() noexcept {
        return !__atomic_test_and_set(&m_locked, __ATOMIC_ACQUIRE);
    }

    void unlock() noexcept {
        __atomic_clear(&m_locked, __ATOMIC_RELEASE);
    }

    [[nodiscard]] bool is_locked() const noexcept {
        return __atomic_load_n(&m_locked, __ATOMIC_RELAXED) != 0;
    }

private:
    volatile uint8_t m_locked;
};

// RAII Spinlock Guard
class SpinlockGuard {
public:
    explicit SpinlockGuard(Spinlock& lock) noexcept : m_lock(lock) {
        m_lock.lock();
    }

    ~SpinlockGuard() noexcept {
        m_lock.unlock();
    }

    SpinlockGuard(const SpinlockGuard&) = delete;
    SpinlockGuard& operator=(const SpinlockGuard&) = delete;

private:
    Spinlock& m_lock;
};

// RAII Interrupt-Safe Spinlock Guard (disables interrupts while holding lock)
class IrqSpinlockGuard {
public:
    explicit IrqSpinlockGuard(Spinlock& lock) noexcept : m_lock(lock) {
        m_rflags = arch::x86_64::save_and_disable_interrupts();
        m_lock.lock();
    }

    ~IrqSpinlockGuard() noexcept {
        m_lock.unlock();
        arch::x86_64::restore_interrupt_state(m_rflags);
    }

    IrqSpinlockGuard(const IrqSpinlockGuard&) = delete;
    IrqSpinlockGuard& operator=(const IrqSpinlockGuard&) = delete;

private:
    Spinlock& m_lock;
    uint64_t  m_rflags;
};

} // namespace llamaos::sync
