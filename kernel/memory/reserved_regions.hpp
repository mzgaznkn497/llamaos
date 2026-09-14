#pragma once

#include "core/types.hpp"
#include "arch/x86_64/boot/multiboot2.hpp"
#include "memory/memory_types.hpp"

// =============================================================================
// LlamaOS/A - Reserved Memory Tracking & Normalization
// =============================================================================
// Tracks physical memory ranges reserved by BIOS/UEFI firmware, ACPI tables,
// the Multiboot2 information structure, kernel code/data/stack sections, and the
// PMM allocation bitmap. Enforces strict reservation precedence over available RAM.
// =============================================================================

namespace llamaos::memory {

struct MemoryRegionRecord {
    PhysicalAddress start{0};
    PhysicalAddress end{0}; // Exclusive: start + length
    uint64_t length{0};
    boot::MemoryType type{boot::MemoryType::Reserved};
    const char* name{"Unknown"};

    [[nodiscard]] constexpr bool is_available() const noexcept {
        return type == boot::MemoryType::Available;
    }

    [[nodiscard]] constexpr bool is_reserved() const noexcept {
        return !is_available();
    }

    [[nodiscard]] constexpr bool contains(PhysicalAddress addr) const noexcept {
        return addr >= start && addr < end;
    }

    [[nodiscard]] constexpr bool overlaps(PhysicalAddress range_start, uint64_t range_len) const noexcept {
        if (range_len == 0 || length == 0) return false;
        PhysicalAddress range_end = range_start + range_len;
        return (start < range_end) && (end > range_start);
    }
};

class ReservedMemoryTracker {
public:
    static constexpr size_t MAX_REGIONS = 128;

    ReservedMemoryTracker() = default;

    // Registers a raw memory region into the tracker and re-normalizes
    bool add_region(PhysicalAddress start, uint64_t length, boot::MemoryType type, const char* name);

    // Populates tracker from Multiboot2 memory map and explicit kernel layout boundaries
    void populate(const boot::BootInfo& boot_info,
                  PhysicalAddress kernel_start,
                  PhysicalAddress kernel_end,
                  PhysicalAddress mb2_paddr,
                  uint64_t mb2_size);

    // Re-registers or updates PMM bitmap range once placed and re-normalizes
    bool register_pmm_bitmap(PhysicalAddress bitmap_start, uint64_t bitmap_size);

    // Queries whether an address or range intersects any reserved region
    [[nodiscard]] bool is_reserved(PhysicalAddress addr) const;
    [[nodiscard]] bool overlaps_reserved(PhysicalAddress start, uint64_t length) const;
    [[nodiscard]] bool is_range_available(PhysicalAddress start, uint64_t length) const;

    // Direct access to normalized disjoint records
    [[nodiscard]] size_t count() const { return m_count; }
    [[nodiscard]] const MemoryRegionRecord& get(size_t index) const { return m_regions[index]; }

    // Direct access to raw records (for auditing)
    [[nodiscard]] size_t raw_count() const { return m_raw_count; }
    [[nodiscard]] const MemoryRegionRecord& get_raw(size_t index) const { return m_raw_regions[index]; }

    // Logs all tracked normalized regions via klog_info
    void dump_regions() const;

    // Computes strictly disjoint, non-overlapping normalized intervals
    void normalize();

private:
    MemoryRegionRecord m_raw_regions[MAX_REGIONS]{};
    size_t m_raw_count{0};

    MemoryRegionRecord m_regions[MAX_REGIONS]{};
    size_t m_count{0};
};

} // namespace llamaos::memory

