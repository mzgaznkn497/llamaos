#pragma once

#include "input_event.hpp"

// =============================================================================
// LlamaOS/A - Bounded Input Event Queue (SPSC Ring Buffer)
// =============================================================================
// Concurrency Contract:
// - Strictly Single-Producer / Single-Consumer (SPSC) lock-free ring buffer.
// - Producer: Solely the IRQ1 Keyboard Interrupt Handler (writes m_head, reads m_tail).
// - Consumer: Solely the Kernel Main Loop (writes m_tail, reads m_head).
// - NOT a general multi-threaded or multi-producer concurrent queue.
// - Deterministic memory ordering: compiler memory barriers ensure store ordering
//   (data stored in buffer before head updated; data read from buffer before tail updated).
// - No shared counter variable: occupancy is derived purely from (head - tail).
// - Power-of-2 capacity guarantees mask-based indexing and well-defined wrap-around.
// - Zero dynamic heap allocation, bounded execution time, and no blocking/sleeping.
// =============================================================================

namespace llamaos::drivers {

template <size_t Capacity = 128>
class InputEventQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    InputEventQueue() = default;

    // Interrupt-safe producer: pushes a KeyEvent into the queue.
    // Executed exclusively in IRQ1 context.
    // If full, drops the event and increments dropped telemetry counter.
    bool push(const KeyEvent& event) noexcept {
        const size_t current_head = m_head;
        const size_t current_tail = m_tail;

        // Capacity check: if distance between head and tail reaches Capacity, queue is full
        if ((current_head - current_tail) >= Capacity) {
            m_dropped_count = m_dropped_count + 1;
            return false;
        }

        m_buffer[current_head & (Capacity - 1)] = event;

        // Compiler memory fence: ensure payload store is committed before head index is updated
        asm volatile("" ::: "memory");
        m_head = current_head + 1;
        return true;
    }

    // Kernel consumer: pops the oldest KeyEvent from the queue.
    // Executed exclusively in kernel context.
    // Returns true if an event was popped, false if queue was empty.
    bool pop(KeyEvent* out_event) noexcept {
        if (!out_event) {
            return false;
        }

        const size_t current_tail = m_tail;
        const size_t current_head = m_head;

        if (current_tail == current_head) {
            return false; // Queue is empty
        }

        *out_event = m_buffer[current_tail & (Capacity - 1)];

        // Compiler memory fence: ensure payload read completes before tail index is updated
        asm volatile("" ::: "memory");
        m_tail = current_tail + 1;
        return true;
    }

    // Capacity & availability queries
    size_t count() const noexcept {
        const size_t h = m_head;
        const size_t t = m_tail;
        return h - t;
    }

    size_t capacity() const noexcept { return Capacity; }
    bool is_empty() const noexcept { return m_head == m_tail; }
    bool is_full() const noexcept { return (m_head - m_tail) >= Capacity; }

    // Telemetry
    uint64_t dropped_count() const noexcept { return m_dropped_count; }

    // Resets queue state (only safe when interrupts are disabled / before driver init)
    void clear() noexcept {
        m_head = 0;
        m_tail = 0;
        m_dropped_count = 0;
    }

private:
    KeyEvent m_buffer[Capacity]{};
    volatile size_t m_head{0};
    volatile size_t m_tail{0};
    volatile uint64_t m_dropped_count{0};
};

// Global default keyboard event queue
using KeyboardEventQueue = InputEventQueue<128>;

} // namespace llamaos::drivers
