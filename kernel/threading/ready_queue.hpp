#pragma once

#include "core/types.hpp"
#include "threading/thread.hpp"

// =============================================================================
// LlamaOS/A - Scheduler Ready Queue
// =============================================================================
// Implements a deterministic, bounded FIFO ready queue of ThreadControlBlock
// pointers with strict duplicate prevention and constant-time operations.
//
// Queue Invariants:
// 1. Capacity is bounded by MAX_CAPACITY (64 slots).
// 2. Duplicate Prevention: No ThreadControlBlock pointer may appear more than once.
// 3. FIFO Scheduling Order: dequeue() always yields the oldest runnable thread.
// 4. Thread State Invariant: Threads entered into the queue MUST be in ThreadState::Ready.
// 5. Non-Runnables Excluded: Terminated, Blocked, Running, and Idle threads are NEVER enqueued.
// =============================================================================

namespace llamaos::threading {

class ReadyQueue {
public:
    static constexpr size_t MAX_CAPACITY = 64;

    constexpr ReadyQueue() = default;

    // Adds a ready thread to the tail of the FIFO queue.
    // Rejects null pointers, duplicate entries, full queue, or invalid state.
    [[nodiscard]] bool enqueue(ThreadControlBlock* thread) noexcept {
        if (!thread) return false;
        if (thread->state != ThreadState::Ready) return false;
        if (is_full()) return false;
        if (contains(thread)) return false; // Duplicate prevention

        m_entries[m_tail] = thread;
        m_tail = (m_tail + 1) % MAX_CAPACITY;
        m_count++;
        return true;
    }

    // Removes and returns the thread at the head of the FIFO queue.
    [[nodiscard]] ThreadControlBlock* dequeue() noexcept {
        if (is_empty()) return nullptr;

        ThreadControlBlock* tcb = m_entries[m_head];
        m_entries[m_head] = nullptr;
        m_head = (m_head + 1) % MAX_CAPACITY;
        m_count--;
        return tcb;
    }

    // Removes a specific thread from the queue if present (e.g. upon unprompted block/exit)
    bool remove(const ThreadControlBlock* thread) noexcept {
        if (!thread || is_empty()) return false;

        size_t idx = m_head;
        for (size_t i = 0; i < m_count; ++i) {
            if (m_entries[idx] == thread) {
                // Found: shift subsequent entries to maintain contiguous circular buffer
                size_t curr = idx;
                for (size_t j = i; j < m_count - 1; ++j) {
                    size_t next_pos = (curr + 1) % MAX_CAPACITY;
                    m_entries[curr] = m_entries[next_pos];
                    curr = next_pos;
                }
                m_tail = (m_tail == 0) ? (MAX_CAPACITY - 1) : (m_tail - 1);
                m_entries[m_tail] = nullptr;
                m_count--;
                return true;
            }
            idx = (idx + 1) % MAX_CAPACITY;
        }
        return false;
    }

    [[nodiscard]] bool contains(const ThreadControlBlock* thread) const noexcept {
        if (!thread || is_empty()) return false;

        size_t idx = m_head;
        for (size_t i = 0; i < m_count; ++i) {
            if (m_entries[idx] == thread) {
                return true;
            }
            idx = (idx + 1) % MAX_CAPACITY;
        }
        return false;
    }

    [[nodiscard]] bool is_empty() const noexcept { return m_count == 0; }
    [[nodiscard]] bool is_full() const noexcept { return m_count >= MAX_CAPACITY; }
    [[nodiscard]] size_t size() const noexcept { return m_count; }
    [[nodiscard]] size_t capacity() const noexcept { return MAX_CAPACITY; }

    void clear() noexcept {
        for (size_t i = 0; i < MAX_CAPACITY; ++i) {
            m_entries[i] = nullptr;
        }
        m_head = 0;
        m_tail = 0;
        m_count = 0;
    }

    [[nodiscard]] bool verify_invariants() const noexcept {
        if (m_count > MAX_CAPACITY) return false;
        if ((m_head + m_count) % MAX_CAPACITY != m_tail) return false;

        size_t idx = m_head;
        for (size_t i = 0; i < m_count; ++i) {
            const ThreadControlBlock* entry = m_entries[idx];
            if (!entry) return false;
            if (entry->state != ThreadState::Ready) return false;
            if (entry->is_idle) return false;

            // Verify no duplicates
            size_t check_idx = (idx + 1) % MAX_CAPACITY;
            for (size_t j = i + 1; j < m_count; ++j) {
                if (m_entries[check_idx] == entry) return false;
                check_idx = (check_idx + 1) % MAX_CAPACITY;
            }
            idx = (idx + 1) % MAX_CAPACITY;
        }
        return true;
    }

private:
    ThreadControlBlock* m_entries[MAX_CAPACITY]{nullptr};
    size_t m_head{0};
    size_t m_tail{0};
    size_t m_count{0};
};

} // namespace llamaos::threading
