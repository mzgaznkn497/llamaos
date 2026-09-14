#pragma once

#include "core/types.hpp"
#include "virtio_defs.hpp"
#include "memory/memory_types.hpp"

// =============================================================================
// LlamaOS/A - Split Virtqueue Management
// =============================================================================
// Manages VirtIO split virtqueue descriptor table, available ring, and used ring.
// Provides descriptor allocation/freeing, buffer chaining, notification, and
// synchronous completion polling with timeout safety.
// =============================================================================

namespace llamaos::drivers::virtio {

class Virtqueue {
public:
    Virtqueue() = default;
    ~Virtqueue();

    // Initializes the virtqueue for a given queue index, queue size, and device I/O base
    bool init(uint16_t queue_index, uint16_t queue_size, uint16_t io_base);

    // Releases allocated physical pages and resets queue state
    void cleanup();

    // Inquiries
    [[nodiscard]] bool is_valid() const noexcept { return m_initialized; }
    [[nodiscard]] uint16_t queue_size() const noexcept { return m_queue_size; }
    [[nodiscard]] uint16_t queue_index() const noexcept { return m_queue_index; }
    [[nodiscard]] memory::PhysicalAddress physical_address() const noexcept { return m_phys_addr; }
    [[nodiscard]] uint16_t last_used_idx() const noexcept { return m_last_used_idx; }
    [[nodiscard]] uint16_t used_idx() const noexcept { return m_used ? m_used->idx : 0; }

    // Descriptor chain management
    [[nodiscard]] int16_t alloc_descriptor();
    void free_descriptor_chain(uint16_t head);

    void set_descriptor(uint16_t index, uint64_t paddr, uint32_t len, uint16_t flags, uint16_t next);

    // Enqueues descriptor chain to Available Ring and notifies hypervisor
    void submit_descriptor_chain(uint16_t head_index);

    // Polls Used Ring until head_index completes or timeout expires
    bool wait_for_completion(uint16_t head_index, uint32_t max_polls = 100000000);

    // Checks if a packet or buffer has arrived on Used Ring (e.g. for RX virtqueue)
    bool has_used_buffer() const noexcept;
    bool pop_used_buffer(uint16_t* out_head, uint32_t* out_len) noexcept;

private:
    uint16_t m_queue_index{0};
    uint16_t m_queue_size{0};
    uint16_t m_io_base{0};

    memory::PhysicalAddress m_phys_addr{0};
    void*                   m_virt_addr{nullptr};
    size_t                  m_total_pages{0};

    VirtqDesc*  m_desc{nullptr};
    VirtqAvail* m_avail{nullptr};
    VirtqUsed*  m_used{nullptr};

    uint16_t m_free_head{0};
    uint16_t m_num_free{0};
    uint16_t m_last_used_idx{0};
    bool     m_initialized{false};
};

} // namespace llamaos::drivers::virtio
