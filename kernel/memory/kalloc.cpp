#include "kalloc.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - Kernel Dynamic Allocation (kmalloc / kfree) Subsystem
// =============================================================================
// Provides robust, leak-free byte-granularity dynamic memory allocation.
// Uses slab freelists for small allocations (64B to 2048B) and direct page
// allocations with headers for large allocations.
// =============================================================================

namespace llamaos::memory {

namespace {

constexpr uint32_t ALLOC_MAGIC = 0x4C4C414D; // "LLAM"

struct alignas(16) AllocHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t page_count;
    uint8_t  slab_bin; // 0xFF = direct page alloc, 0..5 = slab bin
    uint8_t  reserved[3];
};

static_assert(sizeof(AllocHeader) == 16, "AllocHeader must be 16-byte aligned");

struct SlabNode {
    SlabNode* next;
};

constexpr size_t SLAB_SIZES[] = { 64, 128, 256, 512, 1024, 2048 };
constexpr size_t NUM_SLABS = sizeof(SLAB_SIZES) / sizeof(SLAB_SIZES[0]);

SlabNode* g_slab_freelists[NUM_SLABS]{};
sync::Spinlock g_kalloc_lock;

int find_slab_bin(size_t total_needed) {
    for (size_t i = 0; i < NUM_SLABS; ++i) {
        if (total_needed <= SLAB_SIZES[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // anonymous namespace

void* alloc_kernel_pages(PageCount count, PageFlags /*flags*/) {
    if (count.value() == 0) return nullptr;

    PhysicalAddress pa = g_pmm.alloc_pages(count);
    if (pa.is_null()) {
        klog_error("kalloc: Failed to allocate %u physical pages!", static_cast<uint32_t>(count.value()));
        return nullptr;
    }

    void* ptr = phys_to_virt(pa).as_ptr();
    memset(ptr, 0, count.value() * PAGE_SIZE);
    return ptr;
}

bool free_kernel_pages(void* virt_addr, PageCount count) {
    if (!virt_addr || count.value() == 0) return false;

    VirtualAddress va(virt_addr);
    PhysicalAddress pa = virt_to_phys(va);
    return g_pmm.free_pages(pa, count);
}

void* kmalloc(size_t size) {
    if (size == 0) return nullptr;

    sync::IrqSpinlockGuard guard(g_kalloc_lock);

    size_t total_needed = size + sizeof(AllocHeader);
    int bin = find_slab_bin(total_needed);

    if (bin >= 0) {
        size_t block_size = SLAB_SIZES[bin];
        if (!g_slab_freelists[bin]) {
            // Replenish slab bin from 1 page
            void* new_page = alloc_kernel_pages(PageCount(1));
            if (!new_page) return nullptr;

            size_t blocks_per_page = PAGE_SIZE / block_size;
            uint8_t* p = static_cast<uint8_t*>(new_page);
            for (size_t b = 0; b < blocks_per_page; ++b) {
                auto* node = reinterpret_cast<SlabNode*>(p + b * block_size);
                node->next = g_slab_freelists[bin];
                g_slab_freelists[bin] = node;
            }
        }

        auto* node = g_slab_freelists[bin];
        g_slab_freelists[bin] = node->next;

        auto* header = reinterpret_cast<AllocHeader*>(node);
        header->magic = ALLOC_MAGIC;
        header->size = static_cast<uint32_t>(size);
        header->page_count = 0;
        header->slab_bin = static_cast<uint8_t>(bin);

        return reinterpret_cast<void*>(header + 1);
    }

    // Direct multi-page allocation for requests > 2048 bytes
    PageCount pages = PageCount::from_bytes(total_needed);
    void* page_ptr = alloc_kernel_pages(pages);
    if (!page_ptr) return nullptr;

    auto* header = static_cast<AllocHeader*>(page_ptr);
    header->magic = ALLOC_MAGIC;
    header->size = static_cast<uint32_t>(size);
    header->page_count = static_cast<uint32_t>(pages.value());
    header->slab_bin = 0xFF;

    return reinterpret_cast<void*>(header + 1);
}

void kfree(void* ptr) {
    if (!ptr) return;

    sync::IrqSpinlockGuard guard(g_kalloc_lock);

    auto* header = reinterpret_cast<AllocHeader*>(ptr) - 1;
    if (header->magic != ALLOC_MAGIC) {
        // Fallback for legacy page-aligned pointer
        VirtualAddress va(ptr);
        if (va.is_page_aligned()) {
            free_kernel_pages(ptr, PageCount(1));
        }
        return;
    }

    // Clear magic to prevent double free
    header->magic = 0;

    if (header->slab_bin != 0xFF) {
        int bin = static_cast<int>(header->slab_bin);
        if (bin >= 0 && bin < static_cast<int>(NUM_SLABS)) {
            auto* node = reinterpret_cast<SlabNode*>(header);
            node->next = g_slab_freelists[bin];
            g_slab_freelists[bin] = node;
            return;
        }
    }

    if (header->page_count > 0) {
        free_kernel_pages(header, PageCount(header->page_count));
    }
}

} // namespace llamaos::memory
