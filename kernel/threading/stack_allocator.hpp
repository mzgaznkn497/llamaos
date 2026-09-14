#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "threading/thread.hpp"

// =============================================================================
// LlamaOS/A - Kernel Thread Stack Allocator
// =============================================================================
// Manages thread kernel stacks in the dedicated higher-half heap window.
// Every thread stack is backed by physical PMM page frames, mapped through VMM
// with RW NX permissions, and preceded by an unmapped 4 KiB guard page
// to catch stack overflows immediately via Phase 3 Page Fault (#PF) handlers.
// =============================================================================

namespace llamaos::threading {

class StackAllocator {
public:
    static constexpr size_t STACK_USABLE_PAGES  = 4;     // 16 KiB usable stack
    static constexpr size_t GUARD_PAGES          = 1;     // 4 KiB non-present guard page
    static constexpr size_t PAGES_PER_SLOT       = GUARD_PAGES + STACK_USABLE_PAGES; // 5 pages = 20 KiB
    static constexpr uint64_t SLOT_SIZE_BYTES    = PAGES_PER_SLOT * llamaos::PAGE_SIZE;
    static constexpr size_t MAX_STACK_SLOTS      = 64;    // Up to 64 concurrent thread stacks

    // Base virtual address for thread stacks (starts in PDPT[509] dynamic 4 KiB window)
    static constexpr uint64_t THREAD_STACK_BASE_VIRT = 0xFFFFFFFF72000000ULL;

    static void init();

    // Allocates physical frames, maps usable pages in VMM, leaves guard page unmapped
    static bool allocate_stack(ThreadStackInfo* out_info);

    // Unmaps usable pages in VMM and frees physical frames in PMM
    static bool free_stack(const ThreadStackInfo& info);

    // Diagnostic queries
    static size_t allocated_slots_count() noexcept;
    static uint64_t total_pages_allocated() noexcept;
    static uint64_t total_pages_reclaimed() noexcept;
    [[nodiscard]] static bool is_slot_in_use(int32_t slot) noexcept {
        if (slot < 0 || slot >= static_cast<int32_t>(MAX_STACK_SLOTS)) return false;
        return s_slot_in_use[slot];
    }

    // Checks whether a given virtual address falls inside any thread guard page
    static constexpr bool is_guard_page_address(memory::VirtualAddress vaddr) noexcept {
        uint64_t val = vaddr.value();
        uint64_t max_val = THREAD_STACK_BASE_VIRT + MAX_STACK_SLOTS * SLOT_SIZE_BYTES;
        if (val < THREAD_STACK_BASE_VIRT || val >= max_val) {
            return false;
        }
        uint64_t offset = val - THREAD_STACK_BASE_VIRT;
        uint64_t slot_offset = offset % SLOT_SIZE_BYTES;
        return slot_offset < (GUARD_PAGES * llamaos::PAGE_SIZE);
    }

private:
    static bool s_slot_in_use[MAX_STACK_SLOTS];
    static uint64_t s_pages_allocated;
    static uint64_t s_pages_reclaimed;
    static bool s_initialized;
};

} // namespace llamaos::threading
