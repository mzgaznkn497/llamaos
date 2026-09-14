#include "stack_allocator.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Kernel Thread Stack Allocator Implementation
// =============================================================================

namespace llamaos::threading {

using memory::g_pmm;
using memory::g_vmm;
using memory::PhysicalAddress;
using memory::VirtualAddress;
using memory::PageCount;
using memory::PageFlags;
using llamaos::PAGE_SIZE;

bool StackAllocator::s_slot_in_use[MAX_STACK_SLOTS]{false};
uint64_t StackAllocator::s_pages_allocated{0};
uint64_t StackAllocator::s_pages_reclaimed{0};
bool StackAllocator::s_initialized{false};

void StackAllocator::init() {
    for (size_t i = 0; i < MAX_STACK_SLOTS; ++i) {
        s_slot_in_use[i] = false;
    }
    s_pages_allocated = 0;
    s_pages_reclaimed = 0;
    s_initialized = true;
    klog_info("StackAllocator initialized: Base=%p, Slots=%u, StackSize=%u KiB, GuardPage=4 KiB",
              reinterpret_cast<void*>(THREAD_STACK_BASE_VIRT),
              static_cast<uint32_t>(MAX_STACK_SLOTS),
              static_cast<uint32_t>(STACK_USABLE_PAGES * 4));
}

bool StackAllocator::allocate_stack(ThreadStackInfo* out_info) {
    if (!out_info || !s_initialized) return false;

    // 1. Find free slot
    int free_slot = -1;
    for (size_t i = 0; i < MAX_STACK_SLOTS; ++i) {
        if (!s_slot_in_use[i]) {
            free_slot = static_cast<int>(i);
            break;
        }
    }

    if (free_slot < 0) {
        klog_error("StackAllocator: Out of stack slots (all %u allocated)!",
                   static_cast<uint32_t>(MAX_STACK_SLOTS));
        return false;
    }

    // 2. Allocate backing physical pages from PMM
    PhysicalAddress pa = g_pmm.alloc_pages(PageCount(STACK_USABLE_PAGES));
    if (pa.is_null()) {
        klog_error("StackAllocator: Failed to allocate %u physical frames from PMM!",
                   static_cast<uint32_t>(STACK_USABLE_PAGES));
        return false;
    }

    // 3. Compute virtual boundaries
    uint64_t slot_vbase = THREAD_STACK_BASE_VIRT + static_cast<uint64_t>(free_slot) * SLOT_SIZE_BYTES;
    VirtualAddress guard_va(slot_vbase);
    VirtualAddress bottom_va(slot_vbase + GUARD_PAGES * PAGE_SIZE);
    VirtualAddress top_va(slot_vbase + SLOT_SIZE_BYTES);

    // Guard page (guard_va) remains unmapped deliberately.

    // 4. Map usable pages in VMM with Supervisor RW NX permissions
    PageFlags flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    for (size_t p = 0; p < STACK_USABLE_PAGES; ++p) {
        VirtualAddress cur_va = bottom_va + p * PAGE_SIZE;
        PhysicalAddress cur_pa = pa + p * PAGE_SIZE;

        if (g_vmm.map_page(cur_va, cur_pa, flags) != memory::VmmStatus::Success) {
            klog_error("StackAllocator: Failed to map stack page %u at %p!",
                       static_cast<uint32_t>(p), cur_va.as_ptr());
            // Rollback previous mappings
            for (size_t rollback = 0; rollback < p; ++rollback) {
                g_vmm.unmap_page(bottom_va + rollback * PAGE_SIZE);
            }
            g_pmm.free_pages(pa, PageCount(STACK_USABLE_PAGES));
            return false;
        }
    }

    // 5. Populate metadata
    out_info->guard_page = guard_va;
    out_info->stack_bottom = bottom_va;
    out_info->stack_top = top_va;
    out_info->physical_base = pa;
    out_info->usable_bytes = STACK_USABLE_PAGES * PAGE_SIZE;
    out_info->page_count = STACK_USABLE_PAGES;
    out_info->slot_index = free_slot;

    s_slot_in_use[free_slot] = true;
    s_pages_allocated += STACK_USABLE_PAGES;

    return true;
}

bool StackAllocator::free_stack(const ThreadStackInfo& info) {
    if (!s_initialized) return false;
    if (info.slot_index < 0 || info.slot_index >= static_cast<int32_t>(MAX_STACK_SLOTS)) {
        klog_error("StackAllocator: Invalid slot index %d for free!", info.slot_index);
        return false;
    }

    if (!s_slot_in_use[info.slot_index]) {
        klog_warn("StackAllocator: Double-free detected on slot %d!", info.slot_index);
        return false;
    }

    // 1. Unmap usable pages from active VMM page tables
    for (size_t p = 0; p < info.page_count; ++p) {
        VirtualAddress cur_va = info.stack_bottom + p * PAGE_SIZE;
        g_vmm.unmap_page(cur_va);
    }

    // 2. Free backing physical page frames back to PMM
    if (!info.physical_base.is_null()) {
        g_pmm.free_pages(info.physical_base, PageCount(info.page_count));
    }

    s_slot_in_use[info.slot_index] = false;
    s_pages_reclaimed += info.page_count;

    return true;
}

size_t StackAllocator::allocated_slots_count() noexcept {
    size_t count = 0;
    for (size_t i = 0; i < MAX_STACK_SLOTS; ++i) {
        if (s_slot_in_use[i]) count++;
    }
    return count;
}

uint64_t StackAllocator::total_pages_allocated() noexcept {
    return s_pages_allocated;
}

uint64_t StackAllocator::total_pages_reclaimed() noexcept {
    return s_pages_reclaimed;
}

} // namespace llamaos::threading
