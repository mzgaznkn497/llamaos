#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "memory/pmm.hpp"

// =============================================================================
// LlamaOS/A - Virtual Memory Manager (VMM) Foundation
// =============================================================================
// Provides 4-level page table abstraction (PML4, PDPT, PD, PT) for x86-64 Long Mode.
// Implements safe page mapping, unmapping, virtual-to-physical translation,
// page attribute manipulation (including NX and W^X), and TLB invalidation.
// =============================================================================

namespace llamaos::memory {

// Status results for VMM operations
enum class VmmStatus {
    Success,
    NotInitialized,
    InvalidVirtualAddress,
    UnalignedAddress,
    OutOfMemory,
    AlreadyMapped,
    NotMapped,
    HugePageCollision
};

// -----------------------------------------------------------------------------
// PageTableEntry: Raw 64-bit Hardware Page Table Entry (PML4E, PDPTE, PDE, PTE)
// -----------------------------------------------------------------------------
struct alignas(8) PageTableEntry {
    uint64_t raw{0};

    [[nodiscard]] constexpr bool is_present() const noexcept {
        return (raw & (1ULL << 0)) != 0;
    }

    [[nodiscard]] constexpr bool is_writable() const noexcept {
        return (raw & (1ULL << 1)) != 0;
    }

    [[nodiscard]] constexpr bool is_user() const noexcept {
        return (raw & (1ULL << 2)) != 0;
    }

    [[nodiscard]] constexpr bool is_write_through() const noexcept {
        return (raw & (1ULL << 3)) != 0;
    }

    [[nodiscard]] constexpr bool is_cache_disabled() const noexcept {
        return (raw & (1ULL << 4)) != 0;
    }

    [[nodiscard]] constexpr bool is_accessed() const noexcept {
        return (raw & (1ULL << 5)) != 0;
    }

    [[nodiscard]] constexpr bool is_dirty() const noexcept {
        return (raw & (1ULL << 6)) != 0;
    }

    [[nodiscard]] constexpr bool is_huge() const noexcept {
        return (raw & (1ULL << 7)) != 0;
    }

    [[nodiscard]] constexpr bool is_global() const noexcept {
        return (raw & (1ULL << 8)) != 0;
    }

    [[nodiscard]] constexpr bool is_no_execute() const noexcept {
        return (raw & (1ULL << 63)) != 0;
    }

    [[nodiscard]] constexpr PhysicalAddress physical_address() const noexcept {
        return PhysicalAddress(raw & 0x000FFFFFFFFFF000ULL);
    }

    [[nodiscard]] constexpr PageFlags flags() const noexcept {
        uint64_t f = 0;
        if (is_present())        f |= static_cast<uint64_t>(PageFlags::Present);
        if (is_writable())       f |= static_cast<uint64_t>(PageFlags::Writable);
        if (is_user())           f |= static_cast<uint64_t>(PageFlags::User);
        if (is_write_through())  f |= static_cast<uint64_t>(PageFlags::WriteThrough);
        if (is_cache_disabled()) f |= static_cast<uint64_t>(PageFlags::CacheDisable);
        if (is_accessed())       f |= static_cast<uint64_t>(PageFlags::Accessed);
        if (is_dirty())          f |= static_cast<uint64_t>(PageFlags::Dirty);
        if (is_huge())           f |= static_cast<uint64_t>(PageFlags::HugePage);
        if (is_global())         f |= static_cast<uint64_t>(PageFlags::Global);
        if (is_no_execute())     f |= static_cast<uint64_t>(PageFlags::NoExecute);
        return static_cast<PageFlags>(f);
    }

    void set(PhysicalAddress paddr, PageFlags flags) noexcept {
        raw = paddr.value() & 0x000FFFFFFFFFF000ULL;
        uint64_t f = static_cast<uint64_t>(flags);
        raw |= (f & 0x1FFULL);           // Bits 0..8: P, R/W, U/S, PWT, PCD, A, D, PS, G
        raw |= (f & (1ULL << 63));       // Bit 63: NX
    }

    void clear() noexcept {
        raw = 0;
    }
};

static_assert(sizeof(PageTableEntry) == 8, "PageTableEntry must be exactly 8 bytes");

// -----------------------------------------------------------------------------
// PageTable: Represents a 512-entry x86-64 page table (4 KiB aligned)
// -----------------------------------------------------------------------------
struct alignas(4096) PageTable {
    PageTableEntry entries[512];
};

static_assert(sizeof(PageTable) == 4096, "PageTable must be exactly 4096 bytes");

// -----------------------------------------------------------------------------
// VirtualMemoryManager: 4-Level Paging Engine
// -----------------------------------------------------------------------------
class VirtualMemoryManager {
public:
    VirtualMemoryManager() = default;

    // Initializes VMM and migrates from bootstrap page tables to dynamically managed tables
    bool init(PhysicalAddress kernel_start = PhysicalAddress(0x100000),
              PhysicalAddress kernel_end = PhysicalAddress(0x200000));

    // Map a 4 KiB physical frame to a virtual address with specified attribute flags
    VmmStatus map_page(VirtualAddress vaddr, PhysicalAddress paddr, PageFlags flags);

    // Creates an isolated per-process user address space (cloning higher-half kernel space)
    [[nodiscard]] PhysicalAddress create_user_address_space();

    // Destroys an isolated user address space and frees lower-half user page tables
    void destroy_user_address_space(PhysicalAddress pml4_pa);

    // Map a 4 KiB frame into an explicit PML4 page table hierarchy
    VmmStatus map_page_in_table(PhysicalAddress pml4_pa, VirtualAddress vaddr, PhysicalAddress paddr, PageFlags flags);

    // Unmap a previously mapped 4 KiB virtual page and invalidate TLB
    VmmStatus unmap_page(VirtualAddress vaddr);

    // Translate a virtual address into physical address and permissions by walking page tables
    [[nodiscard]] bool translate(VirtualAddress vaddr,
                                 PhysicalAddress* out_paddr,
                                 PageFlags* out_flags = nullptr) const;

    // Check if a virtual address is currently mapped in active page tables
    [[nodiscard]] bool is_mapped(VirtualAddress vaddr) const {
        return translate(vaddr, nullptr, nullptr);
    }

    [[nodiscard]] bool is_null_page_protected() const noexcept {
        return !is_mapped(VirtualAddress(0));
    }

    [[nodiscard]] bool is_identity_mapped() const noexcept {
        return m_root_pml4 != nullptr && m_root_pml4->entries[0].is_present();
    }

    // Invalidate a single TLB entry
    static void invlpg(VirtualAddress vaddr) noexcept {
        asm volatile("invlpg (%0)" :: "r"(vaddr.value()) : "memory");
    }

    // Reload CR3 register (complete TLB flush)
    static void reload_cr3(PhysicalAddress pml4_paddr) noexcept {
        asm volatile("mov %0, %%cr3" :: "r"(pml4_paddr.value()) : "memory");
    }

    [[nodiscard]] PhysicalAddress root_pml4_address() const noexcept { return m_root_pml4_paddr; }
    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }

    // Directly inspect active hardware CR3 page table hierarchy for an address
    [[nodiscard]] bool walk_live_cr3(VirtualAddress vaddr, PageTableEntry* out_pte, PhysicalAddress* out_paddr = nullptr) const;

    // Autonomous runtime verification suite
    bool self_test();

private:
    PhysicalAddress m_root_pml4_paddr{0};
    PageTable* m_root_pml4{nullptr};
    bool m_initialized{false};
};

extern VirtualMemoryManager g_vmm;

} // namespace llamaos::memory
