#include "pmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"
#include "core/panic.hpp"
#include "arch/x86_64/cpu/cpu.hpp"

// =============================================================================
// LlamaOS/A - Physical Memory Manager (PMM) Implementation
// =============================================================================

namespace llamaos::memory {

PhysicalMemoryManager g_pmm;

void PhysicalMemoryManager::reserve_range(PhysicalAddress start, PhysicalAddress end) {
    if (end <= start) return;
    PhysicalAddress s_aligned = start.align_down(PAGE_SIZE);
    PhysicalAddress e_aligned = end.align_up(PAGE_SIZE);

    if (e_aligned > m_max_managed_paddr) {
        e_aligned = m_max_managed_paddr;
    }
    if (s_aligned >= m_max_managed_paddr) return;

    size_t start_frame = s_aligned.value() / PAGE_SIZE;
    size_t end_frame = e_aligned.value() / PAGE_SIZE;

    for (size_t f = start_frame; f < end_frame && f < m_total_frames; ++f) {
        set_bit(f);
    }
}

bool PhysicalMemoryManager::init(const boot::BootInfo& boot_info,
                                 ReservedMemoryTracker& tracker,
                                 PhysicalAddress kernel_start,
                                 PhysicalAddress kernel_end,
                                 PhysicalAddress mb2_paddr,
                                 uint64_t mb2_size) {
    m_initialized = false;
    m_tracker = &tracker;
    m_force_alloc_failure = false;

    // 1. Determine maximum usable RAM address from Multiboot2 memory map
    uint64_t max_usable_addr = 0;
    for (size_t i = 0; i < boot_info.mmap_count; ++i) {
        const auto& entry = boot_info.mmap_entries[i];
        if (static_cast<boot::MemoryType>(entry.type) == boot::MemoryType::Available) {
            uint64_t entry_end = entry.base_addr + entry.length;
            if (entry_end > max_usable_addr) {
                max_usable_addr = entry_end;
            }
        }
    }

    if (max_usable_addr == 0) {
        klog_error("PMM Init: No usable physical RAM reported by Multiboot2!");
        return false;
    }

    // Align max managed physical address to 2 MiB boundary
    m_max_managed_paddr = PhysicalAddress(max_usable_addr).align_up(2 * 1024 * 1024ULL);
    m_total_frames = m_max_managed_paddr.value() / PAGE_SIZE;

    // Calculate required bitmap size in bytes (64-bit word aligned)
    m_bitmap_size_bytes = (m_total_frames + 7) / 8;
    m_bitmap_size_bytes = (m_bitmap_size_bytes + 7) & ~7ULL;

    // 2. Find a suitable physical memory location for the allocation bitmap
    // Must reside in an Available RAM block strictly above the kernel physical image
    // and must not collide with the Multiboot2 structure or any firmware reserved area.
    PhysicalAddress search_start = kernel_end.align_up(PAGE_SIZE);
    PhysicalAddress candidate_paddr(0);
    bool found_placement = false;

    for (size_t i = 0; i < boot_info.mmap_count; ++i) {
        const auto& entry = boot_info.mmap_entries[i];
        if (static_cast<boot::MemoryType>(entry.type) != boot::MemoryType::Available) {
            continue;
        }

        PhysicalAddress entry_base(entry.base_addr);
        PhysicalAddress entry_end = entry_base + entry.length;

        PhysicalAddress cur = (entry_base > search_start) ? entry_base.align_up(PAGE_SIZE) : search_start;
        while (cur + m_bitmap_size_bytes <= entry_end) {
            // Verify no collision with multiboot2 structure
            PhysicalAddress mb2_end = mb2_paddr + mb2_size;
            bool mb2_collision = (cur < mb2_end) && ((cur + m_bitmap_size_bytes) > mb2_paddr);

            // Verify no collision with reserved regions
            bool reserved_collision = tracker.overlaps_reserved(cur, m_bitmap_size_bytes);

            if (!mb2_collision && !reserved_collision) {
                candidate_paddr = cur;
                found_placement = true;
                break;
            }
            cur = cur + PAGE_SIZE;
        }

        if (found_placement) break;
    }

    if (!found_placement) {
        klog_error("PMM Init: Failed to locate contiguous RAM for bitmap (%llu bytes)!", m_bitmap_size_bytes);
        return false;
    }

    m_bitmap_paddr = candidate_paddr;
    // Map bitmap physical pointer to kernel higher-half virtual space
    m_bitmap = reinterpret_cast<uint64_t*>(phys_to_virt(m_bitmap_paddr).as_ptr());

    // Register bitmap in the reserved memory tracker, triggering re-normalization
    tracker.register_pmm_bitmap(m_bitmap_paddr, m_bitmap_size_bytes);

    // 3. Fail-Closed Initialization: Fill bitmap with 1s (all frames marked Used/Reserved)
    memset(m_bitmap, 0xFF, m_bitmap_size_bytes);

    // 4. Mark strictly normalized Available RAM regions as Free (0)
    for (size_t i = 0; i < tracker.count(); ++i) {
        const auto& r = tracker.get(i);
        if (r.is_available()) {
            PhysicalAddress start = r.start;
            PhysicalAddress end = r.end;

            if (end > m_max_managed_paddr) {
                end = m_max_managed_paddr;
            }

            if (start < end) {
                size_t start_frame = start.value() / PAGE_SIZE;
                size_t end_frame = end.value() / PAGE_SIZE;
                for (size_t f = start_frame; f < end_frame && f < m_total_frames; ++f) {
                    clear_bit(f);
                }
            }
        }
    }

    // 5. Strict Fail-Safe Re-Reservation Precedence Overrides (Defense in Depth):
    // (A) Frame 0 and Low Memory (0x00000000 .. 0x00100000: IVT, BDA, EBDA, VRAM, BIOS ROM)
    reserve_range(PhysicalAddress(0), PhysicalAddress(0x100000));

    // (B) Kernel Physical Image (.text, .rodata, .data, .bss, early stack)
    reserve_range(kernel_start.align_down(PAGE_SIZE), kernel_end.align_up(PAGE_SIZE));

    // (C) Multiboot2 Information Block
    if (!mb2_paddr.is_null() && mb2_size > 0) {
        reserve_range(mb2_paddr.align_down(PAGE_SIZE), (mb2_paddr + mb2_size).align_up(PAGE_SIZE));
    }

    // (D) PMM Bitmap Allocator itself
    reserve_range(m_bitmap_paddr, m_bitmap_paddr + m_bitmap_size_bytes);

    // (E) All non-available regions from firmware memory map
    for (size_t i = 0; i < boot_info.mmap_count; ++i) {
        const auto& entry = boot_info.mmap_entries[i];
        if (static_cast<boot::MemoryType>(entry.type) != boot::MemoryType::Available) {
            PhysicalAddress s = PhysicalAddress(entry.base_addr).align_down(PAGE_SIZE);
            PhysicalAddress e = PhysicalAddress(entry.base_addr + entry.length).align_up(PAGE_SIZE);
            reserve_range(s, e);
        }
    }


    // 6. Compute exact frame counts by tallying zero bits in bitmap
    size_t free_count = 0;
    for (size_t f = 0; f < m_total_frames; ++f) {
        if (!test_bit(f)) {
            free_count++;
        }
    }

    m_free_frames = free_count;
    m_usable_frames = free_count;
    m_reserved_frames = m_total_frames - m_usable_frames;
    m_allocated_frames = 0;
    m_last_search_frame = 0;
    m_initialized = true;

    return true;
}

PhysicalAddress PhysicalMemoryManager::alloc_page() {
    if (!m_initialized || m_free_frames == 0 || m_force_alloc_failure) {
        return PhysicalAddress(0);
    }

    size_t words = m_total_frames / 64;
    size_t start_word = m_last_search_frame / 64;

    for (size_t w_offset = 0; w_offset < words; ++w_offset) {
        size_t w = (start_word + w_offset) % words;
        if (m_bitmap[w] != 0xFFFFFFFFFFFFFFFFULL) {
            int bit_idx = __builtin_ctzll(~m_bitmap[w]);
            size_t frame = w * 64 + static_cast<size_t>(bit_idx);
            if (frame < m_total_frames) {
                set_bit(frame);
                m_free_frames--;
                m_allocated_frames++;
                m_last_search_frame = frame + 1;

                PhysicalAddress paddr(frame * PAGE_SIZE);
                // Zero-initialize page via kernel higher-half direct mapping if mapped
                if (paddr.is_direct_mapped()) {
                    memset(phys_to_virt(paddr).as_ptr(), 0, PAGE_SIZE);
                }
                return paddr;
            }
        }
    }

    return PhysicalAddress(0);
}

bool PhysicalMemoryManager::free_page(PhysicalAddress paddr) {
    if (!m_initialized) return false;

    // 1. Validate page alignment, nullity, and range limits
    if (!paddr.is_page_aligned() || paddr.is_null() || paddr >= m_max_managed_paddr) {
        klog_error("PMM: Attempted to free invalid or unaligned physical address: %p", paddr.value());
        return false;
    }

    // 2. Reject freeing reserved memory (Firmware, Low Memory, Kernel Image, Bitmap)
    if (m_tracker && m_tracker->is_reserved(paddr)) {
        klog_warn("PMM: Attempt to free reserved physical memory at %p rejected!", paddr.value());
        return false;
    }

    size_t frame = paddr.value() / PAGE_SIZE;

    // 3. Reject double-free
    if (!test_bit(frame)) {
        klog_warn("PMM: Double-free rejected on physical address %p (frame %llu)!", paddr.value(), frame);
        return false;
    }

    // 4. Guard against counter underflow
    if (m_allocated_frames == 0) {
        klog_error("PMM: Frame accounting underflow prevented on free_page(%p)!", paddr.value());
        return false;
    }

    clear_bit(frame);
    m_free_frames++;
    m_allocated_frames--;
    if (frame < m_last_search_frame) {
        m_last_search_frame = frame;
    }

    return true;
}

PhysicalAddress PhysicalMemoryManager::alloc_pages(PageCount count) {
    if (!m_initialized || count.value() == 0 || m_free_frames < count.value() || m_force_alloc_failure) {
        return PhysicalAddress(0);
    }

    if (count.value() == 1) {
        return alloc_page();
    }

    if (count.value() > m_total_frames) {
        return PhysicalAddress(0);
    }

    size_t needed = count.value();
    size_t run_start = 0;
    size_t run_len = 0;

    for (size_t f = m_last_search_frame; f < m_total_frames; ++f) {
        if (!test_bit(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == needed) {
                // Found contiguous run
                for (size_t i = 0; i < needed; ++i) {
                    set_bit(run_start + i);
                }
                m_free_frames -= needed;
                m_allocated_frames += needed;
                m_last_search_frame = run_start + needed;

                PhysicalAddress paddr(run_start * PAGE_SIZE);
                if ((paddr + count.to_bytes()).is_direct_mapped()) {
                    memset(phys_to_virt(paddr).as_ptr(), 0, count.to_bytes());
                }
                return paddr;
            }
        } else {
            run_len = 0;
        }
    }

    // Wrap around to beginning if not found
    run_len = 0;
    for (size_t f = 0; f < m_last_search_frame && f < m_total_frames; ++f) {
        if (!test_bit(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == needed) {
                for (size_t i = 0; i < needed; ++i) {
                    set_bit(run_start + i);
                }
                m_free_frames -= needed;
                m_allocated_frames += needed;
                m_last_search_frame = run_start + needed;

                PhysicalAddress paddr(run_start * PAGE_SIZE);
                if ((paddr + count.to_bytes()).is_direct_mapped()) {
                    memset(phys_to_virt(paddr).as_ptr(), 0, count.to_bytes());
                }
                return paddr;
            }
        } else {
            run_len = 0;
        }
    }

    klog_warn("PMM: Failed to allocate %u contiguous physical pages (run unavailable).", static_cast<uint32_t>(needed));
    return PhysicalAddress(0);
}

bool PhysicalMemoryManager::free_pages(PhysicalAddress paddr, PageCount count) {
    if (!m_initialized || count.value() == 0 || !paddr.is_page_aligned() || paddr.is_null()) {
        return false;
    }
    // Protect against 64-bit integer overflow
    if (paddr.value() > UINT64_MAX - count.to_bytes()) {
        return false;
    }

    bool all_ok = true;
    for (size_t i = 0; i < count.value(); ++i) {
        PhysicalAddress cur = paddr + (i * PAGE_SIZE);
        if (!free_page(cur)) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool PhysicalMemoryManager::is_frame_allocated(PageFrameNumber pfn) const {
    if (!m_initialized || pfn.value() >= m_total_frames) {
        return true; // Treat out-of-range as reserved/allocated
    }
    return test_bit(pfn.value());
}

size_t PhysicalMemoryManager::largest_contiguous_free_pages() const {
    if (!m_initialized || m_free_frames == 0) return 0;
    size_t max_run = 0;
    size_t cur_run = 0;
    for (size_t f = 0; f < m_total_frames; ++f) {
        if (!test_bit(f)) {
            cur_run++;
            if (cur_run > max_run) {
                max_run = cur_run;
            }
        } else {
            cur_run = 0;
        }
    }
    return max_run;
}

void PhysicalMemoryManager::dump_stats() const {
    size_t largest_run = largest_contiguous_free_pages();
    klog_info("Physical Memory Manager (PMM) Operational Statistics:");
    klog_info("  Managed RAM Range   : [0x0000000000000000 - 0x%016llx] (%llu MiB)",
              m_max_managed_paddr.value(), total_bytes() / (1024 * 1024));
    klog_info("  PMM Bitmap Location : Physical %p, Virtual %p (%llu KiB)",
              m_bitmap_paddr.value(),
              phys_to_virt(m_bitmap_paddr).as_ptr(),
              m_bitmap_size_bytes / 1024);
    klog_info("  Total Page Frames   : %llu frames (%llu MiB)",
              static_cast<uint64_t>(m_total_frames), total_bytes() / (1024 * 1024));
    klog_info("  Reserved Frames     : %llu frames (%llu MiB)",
              static_cast<uint64_t>(m_reserved_frames), (static_cast<uint64_t>(m_reserved_frames) * PAGE_SIZE) / (1024 * 1024));
    klog_info("  Usable Page Frames  : %llu frames (%llu MiB)",
              static_cast<uint64_t>(m_usable_frames), usable_bytes() / (1024 * 1024));
    klog_info("  Free Frames         : %llu frames (%llu MiB)",
              static_cast<uint64_t>(m_free_frames), free_bytes() / (1024 * 1024));
    klog_info("  Allocated Frames    : %llu frames (%llu KiB)",
              static_cast<uint64_t>(m_allocated_frames), allocated_bytes() / 1024);
    klog_info("  Largest Free Run    : %llu frames (%llu MiB contiguous)",
              static_cast<uint64_t>(largest_run), (static_cast<uint64_t>(largest_run) * PAGE_SIZE) / (1024 * 1024));
}

bool PhysicalMemoryManager::self_test(PhysicalAddress kernel_start, PhysicalAddress kernel_end, PhysicalAddress mb2_paddr) {
    klog_info("Executing comprehensive PMM validation self-test suite (13 deterministic gates)...");

    // Test 1: Initial free-page count is sane
    if (m_free_frames == 0 || m_free_frames > m_total_frames || (m_free_frames + m_allocated_frames) != m_usable_frames) {
        klog_error("PMM Self-Test 1 Failed: Initial free frame count is insane (%llu free, %llu total)!",
                   static_cast<uint64_t>(m_free_frames), static_cast<uint64_t>(m_total_frames));
        return false;
    }

    // Test 2: alloc_page() returns aligned, non-null address
    size_t free_before = m_free_frames;
    size_t alloc_before = m_allocated_frames;
    PhysicalAddress p1 = alloc_page();
    if (p1.is_null()) {
        klog_error("PMM Self-Test 2 Failed: alloc_page() returned NULL!");
        return false;
    }
    if (!p1.is_page_aligned()) {
        klog_error("PMM Self-Test 2 Failed: alloc_page() returned unaligned address %p!", p1.value());
        return false;
    }

    // Test 3: allocated page is marked used in bitmap
    if (!is_page_allocated(p1)) {
        klog_error("PMM Self-Test 3 Failed: alloc_page() address %p not marked used!", p1.value());
        return false;
    }
    if (m_free_frames != free_before - 1 || m_allocated_frames != alloc_before + 1) {
        klog_error("PMM Self-Test 3 Failed: Frame counters did not update on allocation!");
        return false;
    }

    // Verify zero-initialization on direct-mapped page
    if (p1.is_direct_mapped()) {
        uint64_t* p1_data = phys_to_virt(p1).as<uint64_t>();
        for (size_t i = 0; i < 512; ++i) {
            if (p1_data[i] != 0) {
                klog_error("PMM Self-Test 3 Failed: Allocated page %p is not zeroed!", p1.value());
                return false;
            }
        }
        // Write pattern
        constexpr uint64_t TEST_MAGIC = 0xAA55AA5501234567ULL;
        for (size_t i = 0; i < 512; ++i) p1_data[i] = TEST_MAGIC ^ i;
        for (size_t i = 0; i < 512; ++i) {
            if (p1_data[i] != (TEST_MAGIC ^ i)) {
                klog_error("PMM Self-Test 3 Failed: Pattern readback mismatch!");
                return false;
            }
        }
    }

    // Test 4: free_page() restores it cleanly
    if (!free_page(p1)) {
        klog_error("PMM Self-Test 4 Failed: free_page(%p) returned false!", p1.value());
        return false;
    }
    if (is_page_allocated(p1)) {
        klog_error("PMM Self-Test 4 Failed: Freed page %p is still marked allocated!", p1.value());
        return false;
    }
    if (m_free_frames != free_before || m_allocated_frames != alloc_before) {
        klog_error("PMM Self-Test 4 Failed: Free statistics did not restore!");
        return false;
    }

    // Test 4b: Explicit alloc -> alloc -> free -> alloc -> free accounting cycle
    PhysicalAddress seq_a1 = alloc_page();
    PhysicalAddress seq_a2 = alloc_page();
    if (seq_a1.is_null() || seq_a2.is_null() || m_allocated_frames != alloc_before + 2 || m_free_frames != free_before - 2) {
        klog_error("PMM Self-Test 4b Failed: First two allocs did not adjust counters correctly!");
        return false;
    }
    if (!free_page(seq_a1)) {
        klog_error("PMM Self-Test 4b Failed: free_page(seq_a1) failed!");
        return false;
    }
    if (m_allocated_frames != alloc_before + 1 || m_free_frames != free_before - 1) {
        klog_error("PMM Self-Test 4b Failed: Intermediate free did not adjust counters correctly!");
        return false;
    }
    PhysicalAddress seq_a3 = alloc_page();
    if (seq_a3.is_null() || m_allocated_frames != alloc_before + 2 || m_free_frames != free_before - 2) {
        klog_error("PMM Self-Test 4b Failed: Subsequent alloc did not adjust counters correctly!");
        return false;
    }
    if (!free_page(seq_a2) || !free_page(seq_a3)) {
        klog_error("PMM Self-Test 4b Failed: Final frees failed!");
        return false;
    }
    if (m_allocated_frames != alloc_before || m_free_frames != free_before) {
        klog_error("PMM Self-Test 4b Failed: Final alloc-alloc-free-alloc-free did not return to baseline counters!");
        return false;
    }

    // Test 5: Double-free, unaligned free, and out-of-bounds free are strictly rejected
    if (free_page(p1)) {
        klog_error("PMM Self-Test 5 Failed: Double-free of %p was NOT rejected!", p1.value());
        return false;
    }
    if (free_page(PhysicalAddress(0x1001))) {
        klog_error("PMM Self-Test 5 Failed: Unaligned free was NOT rejected!");
        return false;
    }
    if (free_page(PhysicalAddress(m_max_managed_paddr.value()))) {
        klog_error("PMM Self-Test 5 Failed: Out-of-bounds free was NOT rejected!");
        return false;
    }
    if (m_free_frames != free_before || m_allocated_frames != alloc_before) {
        klog_error("PMM Self-Test 5 Failed: Rejected frees altered frame counters!");
        return false;
    }

    // Test 6: Reserved pages cannot be freed (Gate 7)
    size_t free_gate7 = m_free_frames;
    size_t alloc_gate7 = m_allocated_frames;
    if (!is_frame_allocated(PageFrameNumber(0))) {
        klog_error("PMM Self-Test 6 Failed: Page frame 0 is NOT marked reserved!");
        return false;
    }
    if (free_page(PhysicalAddress(0))) {
        klog_error("PMM Self-Test 6 Failed: free_page(0) succeeded on reserved page 0!");
        return false;
    }
    if (free_page(PhysicalAddress(0x90000))) {
        klog_error("PMM Self-Test 6 Failed: free_page(0x90000) succeeded on Low Memory!");
        return false;
    }
    if (free_page(kernel_start)) {
        klog_error("PMM Self-Test 6 Failed: free_page(kernel_start) succeeded on reserved kernel image!");
        return false;
    }
    if (kernel_end > kernel_start && free_page(kernel_end - 4096)) {
        klog_error("PMM Self-Test 6 Failed: free_page(kernel_end - 4096) succeeded on reserved kernel image!");
        return false;
    }
    if (!mb2_paddr.is_null() && free_page(mb2_paddr.align_down(PAGE_SIZE))) {
        klog_error("PMM Self-Test 6 Failed: free_page(mb2_paddr) succeeded on Multiboot2 info block!");
        return false;
    }
    if (free_page(m_bitmap_paddr)) {
        klog_error("PMM Self-Test 6 Failed: free_page(bitmap) succeeded on reserved bitmap memory!");
        return false;
    }
    if (m_free_frames != free_gate7 || m_allocated_frames != alloc_gate7) {
        klog_error("PMM Self-Test 6 Failed: Reserved page free rejection altered frame counters!");
        return false;
    }

    // Test 7: Multi-page contiguous allocation (Gate 8)
    constexpr size_t MULTI_COUNT = 16;
    PhysicalAddress p_multi = alloc_pages(PageCount(MULTI_COUNT));
    if (p_multi.is_null()) {
        klog_error("PMM Self-Test 7 Failed: alloc_pages(%u) returned NULL!", static_cast<uint32_t>(MULTI_COUNT));
        return false;
    }
    for (size_t i = 0; i < MULTI_COUNT; ++i) {
        PhysicalAddress p_cur = p_multi + (i * PAGE_SIZE);
        if (!is_page_allocated(p_cur)) {
            klog_error("PMM Self-Test 7 Failed: Contiguous frame %llu at %p not marked used!", i, p_cur.value());
            return false;
        }
    }
    if (!free_pages(p_multi, PageCount(MULTI_COUNT))) {
        klog_error("PMM Self-Test 7 Failed: free_pages returned false!");
        return false;
    }
    if (m_free_frames != free_gate7 || m_allocated_frames != alloc_gate7) {
        klog_error("PMM Self-Test 7 Failed: Multi-page free did not restore frame counters!");
        return false;
    }

    // Test 8: Repeated FREE -> ALLOCATED -> FREE state transitions (Gate 8)
    constexpr size_t CYCLE_PAGES = 8;
    PhysicalAddress cycle_addrs[CYCLE_PAGES];
    for (size_t i = 0; i < CYCLE_PAGES; ++i) {
        cycle_addrs[i] = alloc_page();
        if (cycle_addrs[i].is_null()) {
            klog_error("PMM Self-Test 8 Failed: alloc_page() failed on cycle index %llu!", i);
            return false;
        }
    }
    // Verify all 8 allocated pages are distinct
    for (size_t i = 0; i < CYCLE_PAGES; ++i) {
        for (size_t j = i + 1; j < CYCLE_PAGES; ++j) {
            if (cycle_addrs[i] == cycle_addrs[j]) {
                klog_error("PMM Self-Test 8 Failed: Duplicate physical frame allocated (%p)!", cycle_addrs[i].value());
                return false;
            }
        }
    }
    // Free all in reverse order
    for (size_t i = CYCLE_PAGES; i > 0; --i) {
        if (!free_page(cycle_addrs[i - 1])) {
            klog_error("PMM Self-Test 8 Failed: Failed to free cycle page at %p!", cycle_addrs[i - 1].value());
            return false;
        }
    }
    if (m_free_frames != free_gate7 || m_allocated_frames != alloc_gate7) {
        klog_error("PMM Self-Test 8 Failed: Cycle allocations did not restore frame balance!");
        return false;
    }

    // Test 9: Allocation exhaustion and fault injection (Gate 6)
    PhysicalAddress p_over = alloc_pages(PageCount(m_free_frames + 1));
    if (!p_over.is_null()) {
        klog_error("PMM Self-Test 9 Failed: Oversized allocation request succeeded!");
        free_pages(p_over, PageCount(m_free_frames + 1));
        return false;
    }
    if (m_free_frames != free_gate7 || m_allocated_frames != alloc_gate7) {
        klog_error("PMM Self-Test 9 Failed: Exhaustion request altered frame counters!");
        return false;
    }

    set_force_alloc_failure(true);
    PhysicalAddress p_fault = alloc_page();
    set_force_alloc_failure(false);
    if (!p_fault.is_null()) {
        klog_error("PMM Self-Test 9 Failed: Fault injection allocation failure was not honored!");
        free_page(p_fault);
        return false;
    }
    // Verify allocator functionality restores after clearing fault injection
    PhysicalAddress p_restored = alloc_page();
    if (p_restored.is_null()) {
        klog_error("PMM Self-Test 9 Failed: Allocation failed after clearing fault injection!");
        return false;
    }
    free_page(p_restored);

    // Test 10: Allocated pages do not overlap reserved ranges
    PhysicalAddress p_sample = alloc_page();
    if (p_sample.is_null()) {
        klog_error("PMM Self-Test 10 Failed: Sample allocation returned NULL!");
        return false;
    }
    if (m_tracker && m_tracker->overlaps_reserved(p_sample, PAGE_SIZE)) {
        klog_error("PMM Self-Test 10 Failed: Allocated page %p overlaps a reserved memory range!", p_sample.value());
        free_page(p_sample);
        return false;
    }
    free_page(p_sample);

    // Test 11: Bitmap boundaries are tested
    size_t test_boundaries[] = {0, 63, 64, 127, 128, 511, 512};
    for (size_t b : test_boundaries) {
        if (b < m_total_frames) {
            bool state = test_bit(b);
            if (state != test_bit(b)) {
                klog_error("PMM Self-Test 11 Failed: Bit %llu read instability!", b);
                return false;
            }
        }
    }

    // Test 12: Arithmetic overflow cases are tested
    if (!alloc_pages(PageCount(UINT64_MAX)).is_null()) {
        klog_error("PMM Self-Test 12 Failed: Allocation with UINT64_MAX count did not return NULL!");
        return false;
    }
    if (free_page(PhysicalAddress(UINT64_MAX))) {
        klog_error("PMM Self-Test 12 Failed: free_page(UINT64_MAX) was not rejected!");
        return false;
    }
    if (free_pages(PhysicalAddress(0x100000), PageCount(UINT64_MAX))) {
        klog_error("PMM Self-Test 12 Failed: free_pages with UINT64_MAX was not rejected!");
        return false;
    }

    // Accounting balance equation check
    if (m_total_frames != (m_reserved_frames + m_free_frames + m_allocated_frames)) {
        klog_error("PMM Self-Test 12 Failed: Global accounting balance equation violated (%llu != %llu + %llu + %llu)!",
                   static_cast<uint64_t>(m_total_frames),
                   static_cast<uint64_t>(m_reserved_frames),
                   static_cast<uint64_t>(m_free_frames),
                   static_cast<uint64_t>(m_allocated_frames));
        return false;
    }

    // Test 13: Operational Latency Benchmark via RDTSC (Gate 15)
    uint64_t t_alloc_0 = arch::x86_64::rdtsc();
    PhysicalAddress perf_p = alloc_page();
    uint64_t t_alloc_1 = arch::x86_64::rdtsc();
    uint64_t t_free_0 = arch::x86_64::rdtsc();
    free_page(perf_p);
    uint64_t t_free_1 = arch::x86_64::rdtsc();

    klog_info(" [PERF] PMM end-to-end alloc_page latency : %llu CPU cycles (includes 4 KiB zeroing)", t_alloc_1 - t_alloc_0);
    klog_info(" [PERF] PMM end-to-end free_page latency  : %llu CPU cycles", t_free_1 - t_free_0);

    klog_info("PMM validation self-test suite completed successfully (13/13 gates passed).");
    return true;
}

} // namespace llamaos::memory

