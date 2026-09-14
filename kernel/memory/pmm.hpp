#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "memory/reserved_regions.hpp"
#include "arch/x86_64/boot/multiboot2.hpp"

// =============================================================================
// LlamaOS/A - Physical Memory Manager (PMM)
// =============================================================================
// Manages physical memory page frames (4 KiB) using a fail-closed bitmap allocator.
// Enforces reservation precedence, prevents double-freeing, zero-initializes
// allocated frames for memory safety, and safely handles physical memory > 4 GiB.
// =============================================================================

namespace llamaos::memory {

class PhysicalMemoryManager {
public:
    PhysicalMemoryManager() = default;

    // Initializes PMM from boot info and tracked reservations
    bool init(const boot::BootInfo& boot_info,
              ReservedMemoryTracker& tracker,
              PhysicalAddress kernel_start,
              PhysicalAddress kernel_end,
              PhysicalAddress mb2_paddr,
              uint64_t mb2_size);

    // Single-page allocation and deallocation (zero-initialized)
    [[nodiscard]] PhysicalAddress alloc_page();
    bool free_page(PhysicalAddress paddr);

    // Multi-page contiguous allocation and deallocation (zero-initialized)
    [[nodiscard]] PhysicalAddress alloc_pages(PageCount count);
    bool free_pages(PhysicalAddress paddr, PageCount count);

    // Frame status inquiry
    [[nodiscard]] bool is_frame_allocated(PageFrameNumber pfn) const;
    [[nodiscard]] bool is_page_allocated(PhysicalAddress paddr) const {
        return is_frame_allocated(PageFrameNumber::from_address(paddr));
    }

    // Statistics and limits
    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }
    [[nodiscard]] PhysicalAddress max_managed_address() const noexcept { return m_max_managed_paddr; }
    [[nodiscard]] PhysicalAddress bitmap_address() const noexcept { return m_bitmap_paddr; }
    [[nodiscard]] uint64_t bitmap_size_bytes() const noexcept { return m_bitmap_size_bytes; }

    [[nodiscard]] size_t total_frames() const noexcept { return m_total_frames; }
    [[nodiscard]] size_t usable_frames() const noexcept { return m_usable_frames; }
    [[nodiscard]] size_t reserved_frames() const noexcept { return m_reserved_frames; }
    [[nodiscard]] size_t free_frames() const noexcept { return m_free_frames; }
    [[nodiscard]] size_t allocated_frames() const noexcept { return m_allocated_frames; }

    [[nodiscard]] uint64_t total_bytes() const noexcept { return static_cast<uint64_t>(m_total_frames) * PAGE_SIZE; }
    [[nodiscard]] uint64_t usable_bytes() const noexcept { return static_cast<uint64_t>(m_usable_frames) * PAGE_SIZE; }
    [[nodiscard]] uint64_t free_bytes() const noexcept { return static_cast<uint64_t>(m_free_frames) * PAGE_SIZE; }
    [[nodiscard]] uint64_t allocated_bytes() const noexcept { return static_cast<uint64_t>(m_allocated_frames) * PAGE_SIZE; }

    [[nodiscard]] size_t largest_contiguous_free_pages() const;

    // Test fault injection hooks
    void set_force_alloc_failure(bool fail) noexcept { m_force_alloc_failure = fail; }
    [[nodiscard]] bool is_force_alloc_failure() const noexcept { return m_force_alloc_failure; }

    void dump_stats() const;

    // Autonomous runtime verification suite
    bool self_test(PhysicalAddress kernel_start, PhysicalAddress kernel_end, PhysicalAddress mb2_paddr = PhysicalAddress(0));

private:
    void set_bit(size_t frame_idx) noexcept {
        m_bitmap[frame_idx / 64] |= (1ULL << (frame_idx % 64));
    }

    void clear_bit(size_t frame_idx) noexcept {
        m_bitmap[frame_idx / 64] &= ~(1ULL << (frame_idx % 64));
    }

    [[nodiscard]] bool test_bit(size_t frame_idx) const noexcept {
        return (m_bitmap[frame_idx / 64] & (1ULL << (frame_idx % 64))) != 0;
    }

    void reserve_range(PhysicalAddress start, PhysicalAddress end);

    const ReservedMemoryTracker* m_tracker{nullptr};
    uint64_t* m_bitmap{nullptr};
    PhysicalAddress m_bitmap_paddr{0};
    uint64_t m_bitmap_size_bytes{0};

    PhysicalAddress m_max_managed_paddr{0};
    size_t m_total_frames{0};
    size_t m_usable_frames{0};
    size_t m_reserved_frames{0};
    size_t m_free_frames{0};
    size_t m_allocated_frames{0};

    size_t m_last_search_frame{0};
    bool m_force_alloc_failure{false};
    bool m_initialized{false};
};

extern PhysicalMemoryManager g_pmm;

} // namespace llamaos::memory
