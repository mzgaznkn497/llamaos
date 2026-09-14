#include "reserved_regions.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Reserved Memory Tracking & Normalization Implementation
// =============================================================================

namespace llamaos::memory {

bool ReservedMemoryTracker::add_region(PhysicalAddress start, uint64_t length, boot::MemoryType type, const char* name) {
    if (length == 0 || m_raw_count >= MAX_REGIONS) {
        return false;
    }

    m_raw_regions[m_raw_count].start = start;
    m_raw_regions[m_raw_count].end = start + length;
    m_raw_regions[m_raw_count].length = length;
    m_raw_regions[m_raw_count].type = type;
    m_raw_regions[m_raw_count].name = name ? name : "Unknown";
    m_raw_count++;

    normalize();
    return true;
}

void ReservedMemoryTracker::populate(const boot::BootInfo& boot_info,
                                     PhysicalAddress kernel_start,
                                     PhysicalAddress kernel_end,
                                     PhysicalAddress mb2_paddr,
                                     uint64_t mb2_size) {
    m_raw_count = 0;
    m_count = 0;

    // 1. Re-reserve Page 0 and Low Memory (0x00000000 .. 0x00100000)
    // Page 0 traps NULL pointer dereferences; real mode IVT, BDA, EBDA, VGA VRAM live here.
    m_raw_regions[m_raw_count++] = {
        PhysicalAddress(0), PhysicalAddress(0x100000), 0x100000ULL,
        boot::MemoryType::Reserved, "Low Memory (IVT/BDA/EBDA/VRAM)"
    };

    // 2. Reserve Kernel Image (text, rodata, data, bss, early stack)
    if (kernel_end > kernel_start) {
        uint64_t kernel_size = (kernel_end - kernel_start);
        m_raw_regions[m_raw_count++] = {
            kernel_start, kernel_end, kernel_size,
            boot::MemoryType::Reserved, "Kernel Physical Image"
        };
    }

    // 3. Reserve Multiboot2 Information Block
    if (!mb2_paddr.is_null() && mb2_size > 0) {
        // Page-align the reservation range
        PhysicalAddress mb2_start_aligned = mb2_paddr.align_down(PAGE_SIZE);
        uint64_t mb2_end_raw = mb2_paddr.value() + mb2_size;
        PhysicalAddress mb2_end_aligned = PhysicalAddress((mb2_end_raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
        uint64_t aligned_size = mb2_end_aligned - mb2_start_aligned;
        m_raw_regions[m_raw_count++] = {
            mb2_start_aligned, mb2_end_aligned, aligned_size,
            boot::MemoryType::Reserved, "Multiboot2 Information Block"
        };
    }

    // 4. Import firmware memory map entries
    for (size_t i = 0; i < boot_info.mmap_count && m_raw_count < MAX_REGIONS; ++i) {
        const auto& entry = boot_info.mmap_entries[i];
        if (entry.length == 0) continue;

        PhysicalAddress entry_start(entry.base_addr);
        uint64_t entry_len = entry.length;
        boot::MemoryType entry_type = static_cast<boot::MemoryType>(entry.type);

        const char* desc = "Firmware Reserved";
        switch (entry_type) {
            case boot::MemoryType::Available:
                desc = "Available Usable RAM";
                break;
            case boot::MemoryType::AcpiReclaimable:
                desc = "ACPI Reclaimable Memory";
                break;
            case boot::MemoryType::Nvs:
                desc = "ACPI Non-Volatile Storage (NVS)";
                break;
            case boot::MemoryType::BadRam:
                desc = "Defective BadRAM";
                break;
            case boot::MemoryType::Reserved:
            default:
                desc = "Firmware Reserved";
                break;
        }

        m_raw_regions[m_raw_count++] = {
            entry_start, entry_start + entry_len, entry_len,
            entry_type, desc
        };
    }

    normalize();
}

bool ReservedMemoryTracker::register_pmm_bitmap(PhysicalAddress bitmap_start, uint64_t bitmap_size) {
    if (bitmap_size == 0 || m_raw_count >= MAX_REGIONS) return false;
    PhysicalAddress start_aligned = bitmap_start.align_down(PAGE_SIZE);
    uint64_t end_raw = bitmap_start.value() + bitmap_size;
    PhysicalAddress end_aligned = PhysicalAddress((end_raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    uint64_t aligned_size = end_aligned - start_aligned;

    m_raw_regions[m_raw_count++] = {
        start_aligned, end_aligned, aligned_size,
        boot::MemoryType::Reserved, "PMM Allocation Bitmap"
    };

    normalize();
    return true;
}

void ReservedMemoryTracker::normalize() {
    if (m_raw_count == 0) {
        m_count = 0;
        return;
    }

    // Temporary storage for separating reserved and available entries
    MemoryRegionRecord res_temp[MAX_REGIONS];
    size_t res_count = 0;

    MemoryRegionRecord avail_temp[MAX_REGIONS];
    size_t avail_count = 0;

    // 1. Separate raw entries into page-aligned Reserved and Available
    for (size_t i = 0; i < m_raw_count; ++i) {
        const auto& raw = m_raw_regions[i];
        if (raw.length == 0) continue;
        // Overflow protection on 64-bit integer addition
        if (raw.start.value() > UINT64_MAX - raw.length) continue;

        if (raw.is_reserved()) {
            if (res_count < MAX_REGIONS) {
                // Page-align reservations defensively: start down, end up
                PhysicalAddress s = raw.start.align_down(PAGE_SIZE);
                uint64_t end_raw = raw.start.value() + raw.length;
                PhysicalAddress e = PhysicalAddress((end_raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
                if (e > s) {
                    res_temp[res_count].start = s;
                    res_temp[res_count].end = e;
                    res_temp[res_count].length = e - s;
                    res_temp[res_count].type = raw.type;
                    res_temp[res_count].name = raw.name;
                    res_count++;
                }
            }
        } else {
            if (avail_count < MAX_REGIONS) {
                // Page-align available RAM safely: start up, end down (no partial unaligned pages)
                PhysicalAddress s = raw.start.align_up(PAGE_SIZE);
                PhysicalAddress e = (raw.start + raw.length).align_down(PAGE_SIZE);
                if (e > s) {
                    avail_temp[avail_count].start = s;
                    avail_temp[avail_count].end = e;
                    avail_temp[avail_count].length = e - s;
                    avail_temp[avail_count].type = raw.type;
                    avail_temp[avail_count].name = raw.name;
                    avail_count++;
                }
            }
        }
    }

    // 2. Sort reserved entries by start address
    for (size_t i = 0; i + 1 < res_count; ++i) {
        for (size_t j = 0; j + 1 < res_count - i; ++j) {
            if (res_temp[j].start > res_temp[j + 1].start) {
                MemoryRegionRecord tmp = res_temp[j];
                res_temp[j] = res_temp[j + 1];
                res_temp[j + 1] = tmp;
            }
        }
    }

    // Merge strictly overlapping or contiguous identical-type/name reserved entries
    size_t merged_res_count = 0;
    MemoryRegionRecord merged_res[MAX_REGIONS];
    for (size_t i = 0; i < res_count; ++i) {
        if (merged_res_count == 0) {
            merged_res[merged_res_count++] = res_temp[i];
            continue;
        }

        auto& prev = merged_res[merged_res_count - 1];
        const auto& cur = res_temp[i];

        if (prev.end > cur.start) {
            // Strictly overlapping: expand end if cur extends further
            if (cur.end > prev.end) {
                prev.end = cur.end;
                prev.length = prev.end - prev.start;
            }
        } else if (prev.end == cur.start && prev.type == cur.type && strcmp(prev.name, cur.name) == 0) {
            // Adjacent with identical name and type: merge
            prev.end = cur.end;
            prev.length = prev.end - prev.start;
        } else {
            if (merged_res_count < MAX_REGIONS) {
                merged_res[merged_res_count++] = cur;
            }
        }
    }

    // 3. Sort available entries by start address
    for (size_t i = 0; i + 1 < avail_count; ++i) {
        for (size_t j = 0; j + 1 < avail_count - i; ++j) {
            if (avail_temp[j].start > avail_temp[j + 1].start) {
                MemoryRegionRecord tmp = avail_temp[j];
                avail_temp[j] = avail_temp[j + 1];
                avail_temp[j + 1] = tmp;
            }
        }
    }

    // Merge contiguous or overlapping available entries
    size_t merged_avail_count = 0;
    MemoryRegionRecord current_avail[MAX_REGIONS];
    for (size_t i = 0; i < avail_count; ++i) {
        if (merged_avail_count == 0) {
            current_avail[merged_avail_count++] = avail_temp[i];
            continue;
        }

        auto& prev = current_avail[merged_avail_count - 1];
        const auto& cur = avail_temp[i];

        if (prev.end >= cur.start) {
            if (cur.end > prev.end) {
                prev.end = cur.end;
                prev.length = prev.end - prev.start;
            }
        } else {
            if (merged_avail_count < MAX_REGIONS) {
                current_avail[merged_avail_count++] = cur;
            }
        }
    }

    // 4. Subtract each reserved interval from all available intervals (RESERVED > AVAILABLE)
    MemoryRegionRecord next_avail[MAX_REGIONS];
    for (size_t r = 0; r < merged_res_count; ++r) {
        const auto& res = merged_res[r];
        size_t next_count = 0;

        for (size_t a = 0; a < merged_avail_count; ++a) {
            const auto& av = current_avail[a];

            // No overlap between res and av
            if (res.end <= av.start || res.start >= av.end) {
                if (next_count < MAX_REGIONS) {
                    next_avail[next_count++] = av;
                }
                continue;
            }

            // Overlap detected: clip/split av around res
            // Left non-overlapping slice: [av.start, res.start)
            if (av.start < res.start) {
                if (next_count < MAX_REGIONS) {
                    MemoryRegionRecord left = av;
                    left.end = res.start;
                    left.length = left.end - left.start;
                    next_avail[next_count++] = left;
                }
            }

            // Right non-overlapping slice: [res.end, av.end)
            if (res.end < av.end) {
                if (next_count < MAX_REGIONS) {
                    MemoryRegionRecord right = av;
                    right.start = res.end;
                    right.length = right.end - right.start;
                    next_avail[next_count++] = right;
                }
            }
        }

        merged_avail_count = next_count;
        for (size_t k = 0; k < next_count; ++k) {
            current_avail[k] = next_avail[k];
        }
    }

    // 5. Combine merged reserved entries and disjoint available entries into m_regions
    m_count = 0;
    for (size_t i = 0; i < merged_res_count && m_count < MAX_REGIONS; ++i) {
        m_regions[m_count++] = merged_res[i];
    }
    for (size_t i = 0; i < merged_avail_count && m_count < MAX_REGIONS; ++i) {
        m_regions[m_count++] = current_avail[i];
    }

    // 6. Sort final m_regions strictly by start address
    for (size_t i = 0; i + 1 < m_count; ++i) {
        for (size_t j = 0; j + 1 < m_count - i; ++j) {
            if (m_regions[j].start > m_regions[j + 1].start) {
                MemoryRegionRecord tmp = m_regions[j];
                m_regions[j] = m_regions[j + 1];
                m_regions[j + 1] = tmp;
            }
        }
    }
}

bool ReservedMemoryTracker::is_reserved(PhysicalAddress addr) const {
    for (size_t i = 0; i < m_count; ++i) {
        if (m_regions[i].contains(addr)) {
            // Because intervals are strictly disjoint, addr falls into at most ONE interval.
            return m_regions[i].is_reserved();
        }
    }
    // Fail-closed default: untracked or outside all known regions defaults to reserved
    return true;
}

bool ReservedMemoryTracker::overlaps_reserved(PhysicalAddress start, uint64_t length) const {
    if (length == 0) return false;
    // Protect against 64-bit integer overflow in range calculation
    if (start.value() > UINT64_MAX - length) {
        return true; // Overflowing range is unsafe, treat as reserved
    }
    for (size_t i = 0; i < m_count; ++i) {
        if (m_regions[i].is_reserved() && m_regions[i].overlaps(start, length)) {
            return true;
        }
    }
    return false;
}

bool ReservedMemoryTracker::is_range_available(PhysicalAddress start, uint64_t length) const {
    if (length == 0) return false;
    if (start.value() > UINT64_MAX - length) return false;
    PhysicalAddress end = start + length;
    for (size_t i = 0; i < m_count; ++i) {
        if (m_regions[i].is_available() && m_regions[i].start <= start && m_regions[i].end >= end) {
            return true;
        }
    }
    return false;
}

void ReservedMemoryTracker::dump_regions() const {
    klog_info("Tracked Physical Memory Reservations (%u normalized disjoint intervals):", static_cast<uint32_t>(m_count));
    for (size_t i = 0; i < m_count; ++i) {
        const auto& r = m_regions[i];
        uint64_t size_kib = r.length / 1024ULL;
        uint64_t size_mib = size_kib / 1024ULL;
        if (size_mib > 0) {
            klog_info("  [%02u] [0x%016llx - 0x%016llx] (%4llu MiB) %s: %s",
                      static_cast<uint32_t>(i),
                      r.start.value(),
                      r.end.value(),
                      size_mib,
                      r.is_available() ? "[USABLE]  " : "[RESERVED]",
                      r.name);
        } else {
            klog_info("  [%02u] [0x%016llx - 0x%016llx] (%4llu KiB) %s: %s",
                      static_cast<uint32_t>(i),
                      r.start.value(),
                      r.end.value(),
                      size_kib,
                      r.is_available() ? "[USABLE]  " : "[RESERVED]",
                      r.name);
        }
    }
}

} // namespace llamaos::memory

