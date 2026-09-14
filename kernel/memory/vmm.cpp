#include "vmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"
#include "arch/x86_64/cpu/cpu.hpp"

// =============================================================================
// LlamaOS/A - Virtual Memory Manager (VMM) Implementation
// =============================================================================

extern "C" {
    extern uint8_t _text_start[];
    extern uint8_t _text_end[];
    extern uint8_t _rodata_start[];
    extern uint8_t _rodata_end[];
    extern uint8_t _data_start[];
    extern uint8_t _data_end[];
    extern uint8_t _bss_start[];
    extern uint8_t _bss_end[];
    extern uint8_t kernel_stack_bottom[];
    extern uint8_t kernel_stack_top[];
}

namespace llamaos::memory {

VirtualMemoryManager g_vmm;

bool VirtualMemoryManager::init(PhysicalAddress /*kernel_start*/, PhysicalAddress /*kernel_end*/) {
    m_initialized = false;

    // Step 1: Verify IA32_EFER.NXE is enabled on CPU
    uint64_t efer = arch::x86_64::read_msr(0xC0000080);
    if ((efer & (1ULL << 11)) == 0) {
        klog_error("VMM Init: IA32_EFER.NXE (bit 11) is NOT active on host CPU!");
        return false;
    }

    // Step 2: Allocate dynamic Root PML4 table via PMM
    PhysicalAddress new_pml4_pa = g_pmm.alloc_page();
    if (new_pml4_pa.is_null()) {
        klog_error("VMM Init: Failed to allocate physical page for Root PML4!");
        return false;
    }
    PageTable* new_pml4 = phys_to_virt(new_pml4_pa).as<PageTable>();

    // Step 3: Enforce Null-Page Protection and Remove Early Identity Mapping
    // In new_pml4, entries[0] is initialized to 0 (NOT PRESENT).
    // The entire lower-half 0x0000000000000000 .. 0x00007FFFFFFFFFFF is unmapped.

    // Step 4: Allocate dynamic Higher-Half Kernel PDPT (Level 3) for PML4[511]
    PhysicalAddress new_pdpt_pa = g_pmm.alloc_page();
    if (new_pdpt_pa.is_null()) {
        klog_error("VMM Init: Failed to allocate physical page for Kernel PDPT!");
        g_pmm.free_page(new_pml4_pa);
        return false;
    }
    PageTable* new_pdpt = phys_to_virt(new_pdpt_pa).as<PageTable>();
    new_pml4->entries[511].set(new_pdpt_pa, PageFlags::Present | PageFlags::Writable);

    // Step 5: Allocate dynamic Page Directory for -1 GiB window (PDPT[511]: 1 GiB .. 2 GiB physical)
    PhysicalAddress new_pd2_pa = g_pmm.alloc_page();
    if (new_pd2_pa.is_null()) {
        klog_error("VMM Init: Failed to allocate physical page for Kernel PD2!");
        g_pmm.free_page(new_pdpt_pa);
        g_pmm.free_page(new_pml4_pa);
        return false;
    }
    PageTable* new_pd2 = phys_to_virt(new_pd2_pa).as<PageTable>();
    new_pdpt->entries[511].set(new_pd2_pa, PageFlags::Present | PageFlags::Writable);

    // Populate 512 x 2 MiB large pages for 1 GiB .. 2 GiB physical space with NX enabled
    for (size_t i = 0; i < 512; ++i) {
        PhysicalAddress frame_pa(0x40000000ULL + (i * 2 * 1024 * 1024ULL));
        new_pd2->entries[i].set(frame_pa, PageFlags::Present | PageFlags::Writable | PageFlags::HugePage | PageFlags::NoExecute);
    }

    // Step 6: Allocate dynamic Page Directory for -2 GiB window (PDPT[510]: 0 .. 1 GiB physical)
    PhysicalAddress new_pd1_pa = g_pmm.alloc_page();
    if (new_pd1_pa.is_null()) {
        klog_error("VMM Init: Failed to allocate physical page for Kernel PD1!");
        g_pmm.free_page(new_pd2_pa);
        g_pmm.free_page(new_pdpt_pa);
        g_pmm.free_page(new_pml4_pa);
        return false;
    }
    PageTable* new_pd1 = phys_to_virt(new_pd1_pa).as<PageTable>();
    new_pdpt->entries[510].set(new_pd1_pa, PageFlags::Present | PageFlags::Writable);

    // Step 7: Allocate Level 1 4 KiB Page Table for PD1[0] (First 2 MiB: 0x000000 .. 0x1FFFFF)
    // Splits the initial 2 MiB page to implement granular, hardware-enforced W^X section permissions
    PhysicalAddress new_pt0_pa = g_pmm.alloc_page();
    if (new_pt0_pa.is_null()) {
        klog_error("VMM Init: Failed to allocate physical page for Level 1 PT0!");
        g_pmm.free_page(new_pd1_pa);
        g_pmm.free_page(new_pd2_pa);
        g_pmm.free_page(new_pdpt_pa);
        g_pmm.free_page(new_pml4_pa);
        return false;
    }
    PageTable* new_pt0 = phys_to_virt(new_pt0_pa).as<PageTable>();
    new_pd1->entries[0].set(new_pt0_pa, PageFlags::Present | PageFlags::Writable);

    // Section boundary markers
    uintptr_t text_s   = reinterpret_cast<uintptr_t>(_text_start);
    uintptr_t text_e   = reinterpret_cast<uintptr_t>(_text_end);
    uintptr_t rodata_s = reinterpret_cast<uintptr_t>(_rodata_start);
    uintptr_t rodata_e = reinterpret_cast<uintptr_t>(_rodata_end);
    uintptr_t data_s   = reinterpret_cast<uintptr_t>(_data_start);
    uintptr_t data_e   = reinterpret_cast<uintptr_t>(_data_end);
    uintptr_t bss_s    = reinterpret_cast<uintptr_t>(_bss_start);
    uintptr_t bss_e    = reinterpret_cast<uintptr_t>(_bss_end);

    // Map each of the 512 individual 4 KiB frames in the first 2 MiB with exact section protections
    for (size_t f = 0; f < 512; ++f) {
        uint64_t pa_val = f * 4096ULL;
        uintptr_t va_val = 0xFFFFFFFF80000000ULL + pa_val;
        PhysicalAddress frame_pa(pa_val);

        PageFlags flags;
        if (va_val >= text_s && va_val < text_e) {
            // .text section: Executable, Read-Only (W^X violation prevention)
            flags = PageFlags::Present;
        } else if (va_val >= rodata_s && va_val < rodata_e) {
            // .rodata section: Non-Executable, Read-Only
            flags = PageFlags::Present | PageFlags::NoExecute;
        } else if (va_val >= data_s && va_val < data_e) {
            // .data section: Non-Executable, Writable
            flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
        } else if (va_val >= bss_s && va_val < bss_e) {
            // .bss section & stack: Non-Executable, Writable
            flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
        } else if (pa_val < 0x100000ULL) {
            // Low memory (0..1 MiB: IVT, BDA, EBDA, VGA text buffer at 0xB8000, Multiboot2 info)
            flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
        } else if (pa_val >= 0x100000ULL && va_val < text_s) {
            // Early boot code/data (.boot sections)
            flags = PageFlags::Present | PageFlags::Writable;
        } else {
            // Remainder of first 2 MiB (PMM bitmap, initial page pool)
            flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
        }

        new_pt0->entries[f].set(frame_pa, flags);
    }

    // Step 8: Populate entries 1..255 of PD1 with 2 MiB large pages (2 MiB .. 512 MiB physical).
    // Entries 256..511 (0xFFFFFFFFA0000000 .. 0xFFFFFFFFC0000000) are reserved for the
    // dedicated higher-half MMIO/Framebuffer dynamic mapping window.
    for (size_t i = 1; i < 256; ++i) {
        PhysicalAddress frame_pa(i * 2 * 1024 * 1024ULL);
        new_pd1->entries[i].set(frame_pa, PageFlags::Present | PageFlags::Writable | PageFlags::HugePage | PageFlags::NoExecute);
    }

    // Step 9: Reload GDTR with higher-half address to ensure no low physical accesses during transition
    arch::x86_64::Gdtr gdtr{};
    arch::x86_64::sgdt(gdtr);
    if (gdtr.base < 0xFFFF800000000000ULL) {
        gdtr.base += 0xFFFFFFFF80000000ULL;
        arch::x86_64::lgdt(gdtr);
    }

    // Step 10: Switch hardware CR3 to newly constructed dynamic root PML4
    m_root_pml4_paddr = new_pml4_pa;
    m_root_pml4 = new_pml4;
    reload_cr3(new_pml4_pa);

    m_initialized = true;
    klog_info("VMM: Migrated from bootstrap paging to dynamic 4-level page tables.");
    klog_info("VMM: Early identity map removed. Hardware Null-Page Protection active.");
    klog_info("VMM: Granular W^X section permissions enforced (.text=RX, .rodata=R, .data=RW NX, .bss=RW NX).");
    return true;
}

VmmStatus VirtualMemoryManager::map_page(VirtualAddress vaddr, PhysicalAddress paddr, PageFlags flags) {
    return map_page_in_table(m_root_pml4_paddr, vaddr, paddr, flags);
}

VmmStatus VirtualMemoryManager::map_page_in_table(PhysicalAddress pml4_pa, VirtualAddress vaddr, PhysicalAddress paddr, PageFlags flags) {
    if (!m_initialized) {
        return VmmStatus::NotInitialized;
    }
    if (!vaddr.is_canonical()) {
        return VmmStatus::InvalidVirtualAddress;
    }
    if (!vaddr.is_page_aligned() || !paddr.is_page_aligned()) {
        return VmmStatus::UnalignedAddress;
    }

    PageTable* pml4 = pml4_pa.is_null() ? m_root_pml4 : phys_to_virt(pml4_pa).as<PageTable>();

    // Common flags for intermediate paging structures (PML4E, PDPTE, PDE)
    PageFlags table_flags = PageFlags::Present | PageFlags::Writable;
    if (test_flag(flags, PageFlags::User)) {
        table_flags |= PageFlags::User;
    }

    // Level 4: PML4
    size_t pml4_idx = vaddr.pml4_index();
    PageTableEntry& pml4e = pml4->entries[pml4_idx];
    if (!pml4e.is_present()) {
        PhysicalAddress pdpt_pa = g_pmm.alloc_page();
        if (pdpt_pa.is_null()) {
            return VmmStatus::OutOfMemory;
        }
        pml4e.set(pdpt_pa, table_flags);
    } else if (test_flag(flags, PageFlags::User) && !pml4e.is_user()) {
        pml4e.set(pml4e.physical_address(), pml4e.flags() | PageFlags::User);
    }

    PageTable* pdpt = phys_to_virt(pml4e.physical_address()).as<PageTable>();

    // Level 3: PDPT
    size_t pdpt_idx = vaddr.pdpt_index();
    PageTableEntry& pdpte = pdpt->entries[pdpt_idx];
    if (!pdpte.is_present()) {
        PhysicalAddress pd_pa = g_pmm.alloc_page();
        if (pd_pa.is_null()) {
            return VmmStatus::OutOfMemory;
        }
        pdpte.set(pd_pa, table_flags);
    } else if (test_flag(flags, PageFlags::User) && !pdpte.is_user()) {
        pdpte.set(pdpte.physical_address(), pdpte.flags() | PageFlags::User);
    }

    PageTable* pd = phys_to_virt(pdpte.physical_address()).as<PageTable>();

    // Level 2: PD
    size_t pd_idx = vaddr.pd_index();
    PageTableEntry& pde = pd->entries[pd_idx];
    if (pde.is_huge()) {
        // Cannot map a 4 KiB page into a 2 MiB large page without splitting
        return VmmStatus::HugePageCollision;
    }

    if (!pde.is_present()) {
        PhysicalAddress pt_pa = g_pmm.alloc_page();
        if (pt_pa.is_null()) {
            return VmmStatus::OutOfMemory;
        }
        pde.set(pt_pa, table_flags);
    } else if (test_flag(flags, PageFlags::User) && !pde.is_user()) {
        pde.set(pde.physical_address(), pde.flags() | PageFlags::User);
    }

    PageTable* pt = phys_to_virt(pde.physical_address()).as<PageTable>();

    // Level 1: PT
    size_t pt_idx = vaddr.pt_index();
    PageTableEntry& pte = pt->entries[pt_idx];
    if (pte.is_present()) {
        return VmmStatus::AlreadyMapped;
    }

    pte.set(paddr, flags | PageFlags::Present);
    invlpg(vaddr);

    return VmmStatus::Success;
}

PhysicalAddress VirtualMemoryManager::create_user_address_space() {
    if (!m_initialized || !m_root_pml4) return PhysicalAddress(0);

    PhysicalAddress pml4_pa = g_pmm.alloc_page();
    if (pml4_pa.is_null()) return PhysicalAddress(0);

    PageTable* new_pml4 = phys_to_virt(pml4_pa).as<PageTable>();
    // Zero out lower half (user space: 0..255)
    for (size_t i = 0; i < 256; ++i) {
        new_pml4->entries[i].clear();
    }
    // Copy higher half (kernel space: 256..511)
    for (size_t i = 256; i < 512; ++i) {
        new_pml4->entries[i] = m_root_pml4->entries[i];
    }

    return pml4_pa;
}

void VirtualMemoryManager::destroy_user_address_space(PhysicalAddress pml4_pa) {
    if (pml4_pa.is_null() || pml4_pa == m_root_pml4_paddr) return;

    PageTable* pml4 = phys_to_virt(pml4_pa).as<PageTable>();
    for (size_t i = 0; i < 256; ++i) {
        if (pml4->entries[i].is_present()) {
            PageTable* pdpt = phys_to_virt(pml4->entries[i].physical_address()).as<PageTable>();
            for (size_t j = 0; j < 512; ++j) {
                if (pdpt->entries[j].is_present() && !pdpt->entries[j].is_huge()) {
                    PageTable* pd = phys_to_virt(pdpt->entries[j].physical_address()).as<PageTable>();
                    for (size_t k = 0; k < 512; ++k) {
                        if (pd->entries[k].is_present() && !pd->entries[k].is_huge()) {
                            PageTable* pt = phys_to_virt(pd->entries[k].physical_address()).as<PageTable>();
                            for (size_t l = 0; l < 512; ++l) {
                                if (pt->entries[l].is_present()) {
                                    g_pmm.free_page(pt->entries[l].physical_address());
                                    pt->entries[l].clear();
                                }
                            }
                            g_pmm.free_page(pd->entries[k].physical_address());
                        }
                    }
                    g_pmm.free_page(pdpt->entries[j].physical_address());
                }
            }
            g_pmm.free_page(pml4->entries[i].physical_address());
        }
    }
    g_pmm.free_page(pml4_pa);
}

VmmStatus VirtualMemoryManager::unmap_page(VirtualAddress vaddr) {
    if (!m_initialized) {
        return VmmStatus::NotInitialized;
    }
    if (!vaddr.is_canonical()) {
        return VmmStatus::InvalidVirtualAddress;
    }
    if (!vaddr.is_page_aligned()) {
        return VmmStatus::UnalignedAddress;
    }

    size_t pml4_idx = vaddr.pml4_index();
    PageTableEntry& pml4e = m_root_pml4->entries[pml4_idx];
    if (!pml4e.is_present()) {
        return VmmStatus::NotMapped;
    }

    PageTable* pdpt = phys_to_virt(pml4e.physical_address()).as<PageTable>();
    size_t pdpt_idx = vaddr.pdpt_index();
    PageTableEntry& pdpte = pdpt->entries[pdpt_idx];
    if (!pdpte.is_present()) {
        return VmmStatus::NotMapped;
    }

    PageTable* pd = phys_to_virt(pdpte.physical_address()).as<PageTable>();
    size_t pd_idx = vaddr.pd_index();
    PageTableEntry& pde = pd->entries[pd_idx];
    if (!pde.is_present()) {
        return VmmStatus::NotMapped;
    }
    if (pde.is_huge()) {
        return VmmStatus::HugePageCollision;
    }

    PageTable* pt = phys_to_virt(pde.physical_address()).as<PageTable>();
    size_t pt_idx = vaddr.pt_index();
    PageTableEntry& pte = pt->entries[pt_idx];
    if (!pte.is_present()) {
        return VmmStatus::NotMapped;
    }

    pte.clear();
    invlpg(vaddr);

    return VmmStatus::Success;
}

bool VirtualMemoryManager::translate(VirtualAddress vaddr,
                                     PhysicalAddress* out_paddr,
                                     PageFlags* out_flags) const {
    if (!m_initialized || !vaddr.is_canonical()) {
        return false;
    }

    uint64_t cr3 = arch::x86_64::read_cr3();
    PhysicalAddress pml4_pa = (cr3 != 0) ? PhysicalAddress(cr3 & ~0xFFFULL) : m_root_pml4_paddr;
    const PageTable* pml4 = phys_to_virt(pml4_pa).as<PageTable>();

    size_t pml4_idx = vaddr.pml4_index();
    const PageTableEntry& pml4e = pml4->entries[pml4_idx];
    if (!pml4e.is_present()) {
        return false;
    }

    const PageTable* pdpt = phys_to_virt(pml4e.physical_address()).as<PageTable>();
    size_t pdpt_idx = vaddr.pdpt_index();
    const PageTableEntry& pdpte = pdpt->entries[pdpt_idx];
    if (!pdpte.is_present()) {
        return false;
    }

    const PageTable* pd = phys_to_virt(pdpte.physical_address()).as<PageTable>();
    size_t pd_idx = vaddr.pd_index();
    const PageTableEntry& pde = pd->entries[pd_idx];
    if (!pde.is_present()) {
        return false;
    }

    // Check for 2 MiB large page
    if (pde.is_huge()) {
        uint64_t offset_2mb = vaddr.value() & 0x1FFFFFULL;
        if (out_paddr) {
            *out_paddr = pde.physical_address() + offset_2mb;
        }
        if (out_flags) {
            *out_flags = pde.flags();
        }
        return true;
    }

    // 4 KiB standard page
    const PageTable* pt = phys_to_virt(pde.physical_address()).as<PageTable>();
    size_t pt_idx = vaddr.pt_index();
    const PageTableEntry& pte = pt->entries[pt_idx];
    if (!pte.is_present()) {
        return false;
    }

    if (out_paddr) {
        *out_paddr = pte.physical_address() + vaddr.page_offset();
    }
    if (out_flags) {
        *out_flags = pte.flags();
    }
    return true;
}

bool VirtualMemoryManager::walk_live_cr3(VirtualAddress vaddr, PageTableEntry* out_pte, PhysicalAddress* out_paddr) const {
    if (!vaddr.is_canonical()) return false;
    uint64_t cr3 = arch::x86_64::read_cr3();
    PhysicalAddress pml4_pa(cr3 & ~0xFFFULL);
    const PageTable* pml4 = phys_to_virt(pml4_pa).as<PageTable>();

    size_t pml4_idx = vaddr.pml4_index();
    const PageTableEntry& pml4e = pml4->entries[pml4_idx];
    if (!pml4e.is_present()) return false;

    const PageTable* pdpt = phys_to_virt(pml4e.physical_address()).as<PageTable>();
    size_t pdpt_idx = vaddr.pdpt_index();
    const PageTableEntry& pdpte = pdpt->entries[pdpt_idx];
    if (!pdpte.is_present()) return false;

    const PageTable* pd = phys_to_virt(pdpte.physical_address()).as<PageTable>();
    size_t pd_idx = vaddr.pd_index();
    const PageTableEntry& pde = pd->entries[pd_idx];
    if (!pde.is_present()) return false;

    if (pde.is_huge()) {
        if (out_pte) *out_pte = pde;
        if (out_paddr) *out_paddr = pde.physical_address() + (vaddr.value() & 0x1FFFFFULL);
        return true;
    }

    const PageTable* pt = phys_to_virt(pde.physical_address()).as<PageTable>();
    size_t pt_idx = vaddr.pt_index();
    const PageTableEntry& pte = pt->entries[pt_idx];
    if (!pte.is_present()) return false;

    if (out_pte) *out_pte = pte;
    if (out_paddr) *out_paddr = pte.physical_address() + vaddr.page_offset();
    return true;
}

bool VirtualMemoryManager::self_test() {
    klog_info("Executing comprehensive VMM validation self-test suite (14 deterministic gates)...");

    // Test 1: Live Hardware CR3 Page Table Walk & W^X on Kernel Code (.text) [Gates 3 & 4]
    VirtualAddress text_va(reinterpret_cast<uintptr_t>(_text_start));
    PageTableEntry text_pte{};
    PhysicalAddress text_pa(0);
    if (!walk_live_cr3(text_va, &text_pte, &text_pa)) {
        klog_error("VMM Self-Test 1 Failed: CR3 walk failed for kernel .text virtual address %p!", text_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] .text    VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              text_va.value(), text_pa.value(),
              static_cast<uint32_t>(text_va.pml4_index()), static_cast<uint32_t>(text_va.pdpt_index()),
              static_cast<uint32_t>(text_va.pd_index()), static_cast<uint32_t>(text_va.pt_index()),
              text_pte.raw,
              text_pte.is_present() ? 1 : 0, text_pte.is_writable() ? 1 : 0,
              text_pte.is_user() ? 1 : 0, text_pte.is_no_execute() ? 1 : 0);

    if (!text_pte.is_present() || text_pte.is_writable() || text_pte.is_user() || text_pte.is_no_execute()) {
        klog_error("VMM Self-Test 1 Failed: Live PTE permissions incorrect for .text (must be P=1, W=0, U=0, NX=0)!");
        return false;
    }

    // Test 2: Live Hardware CR3 Page Table Walk on Kernel Read-Only Data (.rodata) [Gates 3 & 4]
    VirtualAddress rodata_va(reinterpret_cast<uintptr_t>(_rodata_start));
    PageTableEntry rodata_pte{};
    PhysicalAddress rodata_pa(0);
    if (!walk_live_cr3(rodata_va, &rodata_pte, &rodata_pa)) {
        klog_error("VMM Self-Test 2 Failed: CR3 walk failed for kernel .rodata address %p!", rodata_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] .rodata  VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              rodata_va.value(), rodata_pa.value(),
              static_cast<uint32_t>(rodata_va.pml4_index()), static_cast<uint32_t>(rodata_va.pdpt_index()),
              static_cast<uint32_t>(rodata_va.pd_index()), static_cast<uint32_t>(rodata_va.pt_index()),
              rodata_pte.raw,
              rodata_pte.is_present() ? 1 : 0, rodata_pte.is_writable() ? 1 : 0,
              rodata_pte.is_user() ? 1 : 0, rodata_pte.is_no_execute() ? 1 : 0);

    if (!rodata_pte.is_present() || rodata_pte.is_writable() || rodata_pte.is_user() || !rodata_pte.is_no_execute()) {
        klog_error("VMM Self-Test 2 Failed: Live PTE permissions incorrect for .rodata (must be P=1, W=0, U=0, NX=1)!");
        return false;
    }

    // Test 3: Live Hardware CR3 Page Table Walk on Kernel Read/Write Data (.data) [Gates 3 & 4]
    VirtualAddress data_va(reinterpret_cast<uintptr_t>(_data_start));
    PageTableEntry data_pte{};
    PhysicalAddress data_pa(0);
    if (!walk_live_cr3(data_va, &data_pte, &data_pa)) {
        klog_error("VMM Self-Test 3 Failed: CR3 walk failed for kernel .data address %p!", data_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] .data    VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              data_va.value(), data_pa.value(),
              static_cast<uint32_t>(data_va.pml4_index()), static_cast<uint32_t>(data_va.pdpt_index()),
              static_cast<uint32_t>(data_va.pd_index()), static_cast<uint32_t>(data_va.pt_index()),
              data_pte.raw,
              data_pte.is_present() ? 1 : 0, data_pte.is_writable() ? 1 : 0,
              data_pte.is_user() ? 1 : 0, data_pte.is_no_execute() ? 1 : 0);

    if (!data_pte.is_present() || !data_pte.is_writable() || data_pte.is_user() || !data_pte.is_no_execute()) {
        klog_error("VMM Self-Test 3 Failed: Live PTE permissions incorrect for .data (must be P=1, W=1, U=0, NX=1)!");
        return false;
    }

    // Test 4: Live Hardware CR3 Page Table Walk on Kernel BSS (.bss) [Gates 3 & 4]
    VirtualAddress bss_va(reinterpret_cast<uintptr_t>(_bss_start));
    PageTableEntry bss_pte{};
    PhysicalAddress bss_pa(0);
    if (!walk_live_cr3(bss_va, &bss_pte, &bss_pa)) {
        klog_error("VMM Self-Test 4 Failed: CR3 walk failed for kernel .bss address %p!", bss_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] .bss     VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              bss_va.value(), bss_pa.value(),
              static_cast<uint32_t>(bss_va.pml4_index()), static_cast<uint32_t>(bss_va.pdpt_index()),
              static_cast<uint32_t>(bss_va.pd_index()), static_cast<uint32_t>(bss_va.pt_index()),
              bss_pte.raw,
              bss_pte.is_present() ? 1 : 0, bss_pte.is_writable() ? 1 : 0,
              bss_pte.is_user() ? 1 : 0, bss_pte.is_no_execute() ? 1 : 0);

    if (!bss_pte.is_present() || !bss_pte.is_writable() || bss_pte.is_user() || !bss_pte.is_no_execute()) {
        klog_error("VMM Self-Test 4 Failed: Live PTE permissions incorrect for .bss (must be P=1, W=1, U=0, NX=1)!");
        return false;
    }

    // Test 5: Live Hardware CR3 Page Table Walk on Kernel Stack [Gates 3 & 4]
    VirtualAddress stack_va(reinterpret_cast<uintptr_t>(kernel_stack_bottom));
    PageTableEntry stack_pte{};
    PhysicalAddress stack_pa(0);
    if (!walk_live_cr3(stack_va, &stack_pte, &stack_pa)) {
        klog_error("VMM Self-Test 5 Failed: CR3 walk failed for kernel stack address %p!", stack_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] stack    VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              stack_va.value(), stack_pa.value(),
              static_cast<uint32_t>(stack_va.pml4_index()), static_cast<uint32_t>(stack_va.pdpt_index()),
              static_cast<uint32_t>(stack_va.pd_index()), static_cast<uint32_t>(stack_va.pt_index()),
              stack_pte.raw,
              stack_pte.is_present() ? 1 : 0, stack_pte.is_writable() ? 1 : 0,
              stack_pte.is_user() ? 1 : 0, stack_pte.is_no_execute() ? 1 : 0);

    if (!stack_pte.is_present() || !stack_pte.is_writable() || stack_pte.is_user() || !stack_pte.is_no_execute()) {
        klog_error("VMM Self-Test 5 Failed: Live PTE permissions incorrect for stack (must be P=1, W=1, U=0, NX=1)!");
        return false;
    }

    // Known Higher-Half Address Walk (PMM bitmap buffer)
    VirtualAddress high_va = phys_to_virt(g_pmm.bitmap_address());
    PageTableEntry high_pte{};
    PhysicalAddress high_pa(0);
    if (!walk_live_cr3(high_va, &high_pte, &high_pa)) {
        klog_error("VMM Self-Test Failed: CR3 walk failed for known higher-half bitmap address %p!", high_va.value());
        return false;
    }
    klog_info(" [HARDWARE-VMM] high-mem VA=%p -> PA=%p [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PTE=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              high_va.value(), high_pa.value(),
              static_cast<uint32_t>(high_va.pml4_index()), static_cast<uint32_t>(high_va.pdpt_index()),
              static_cast<uint32_t>(high_va.pd_index()), static_cast<uint32_t>(high_va.pt_index()),
              high_pte.raw,
              high_pte.is_present() ? 1 : 0, high_pte.is_writable() ? 1 : 0,
              high_pte.is_user() ? 1 : 0, high_pte.is_no_execute() ? 1 : 0);

    // Test 6: Null-Page & Lower-Half Unmapped Verification [Gates 4 & 5]
    uint64_t cr3 = arch::x86_64::read_cr3();
    PhysicalAddress pml4_pa(cr3 & ~0xFFFULL);
    const PageTable* live_pml4 = phys_to_virt(pml4_pa).as<PageTable>();

    VirtualAddress null_va(0);
    klog_info(" [HARDWARE-VMM] null-va  VA=%p -> UNMAPPED [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PML4E=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              null_va.value(),
              static_cast<uint32_t>(null_va.pml4_index()), static_cast<uint32_t>(null_va.pdpt_index()),
              static_cast<uint32_t>(null_va.pd_index()), static_cast<uint32_t>(null_va.pt_index()),
              live_pml4->entries[0].raw,
              live_pml4->entries[0].is_present() ? 1 : 0, live_pml4->entries[0].is_writable() ? 1 : 0,
              live_pml4->entries[0].is_user() ? 1 : 0, live_pml4->entries[0].is_no_execute() ? 1 : 0);

    VirtualAddress low_rep_va(0x00007FFFFFFFF000ULL);
    klog_info(" [HARDWARE-VMM] low-half VA=%p -> UNMAPPED [PML4=%u, PDPT=%u, PD=%u, PT=%u] Raw PML4E=0x%016llx (P=%u, W=%u, U=%u, NX=%u)",
              low_rep_va.value(),
              static_cast<uint32_t>(low_rep_va.pml4_index()), static_cast<uint32_t>(low_rep_va.pdpt_index()),
              static_cast<uint32_t>(low_rep_va.pd_index()), static_cast<uint32_t>(low_rep_va.pt_index()),
              live_pml4->entries[low_rep_va.pml4_index()].raw,
              live_pml4->entries[low_rep_va.pml4_index()].is_present() ? 1 : 0, live_pml4->entries[low_rep_va.pml4_index()].is_writable() ? 1 : 0,
              live_pml4->entries[low_rep_va.pml4_index()].is_user() ? 1 : 0, live_pml4->entries[low_rep_va.pml4_index()].is_no_execute() ? 1 : 0);

    if (live_pml4->entries[0].is_present()) {
        klog_error("VMM Self-Test 6 Failed: Live CR3 PML4[0] is marked Present (%016llx)!", live_pml4->entries[0].raw);
        return false;
    }
    for (size_t i = 0; i < 256; ++i) {
        if (live_pml4->entries[i].is_present()) {
            klog_error("VMM Self-Test 6 Failed: Lower-half PML4[%u] is present (%016llx)!",
                       static_cast<uint32_t>(i), live_pml4->entries[i].raw);
            return false;
        }
    }
    // Explicit translation queries for lower-half addresses must fail
    PhysicalAddress unmapped_query_pa;
    if (translate(null_va, &unmapped_query_pa)) {
        klog_error("VMM Self-Test 6 Failed: Virtual address 0x0 unexpectedly translated to %p!", unmapped_query_pa.value());
        return false;
    }
    if (translate(low_rep_va, &unmapped_query_pa)) {
        klog_error("VMM Self-Test 6 Failed: Lower-half address %p unexpectedly translated to %p!", low_rep_va.value(), unmapped_query_pa.value());
        return false;
    }
    // Translation queries for higher-half kernel addresses must succeed
    PhysicalAddress kernel_query_pa;
    if (!translate(text_va, &kernel_query_pa) || !translate(rodata_va, &kernel_query_pa) ||
        !translate(data_va, &kernel_query_pa) || !translate(bss_va, &kernel_query_pa)) {
        klog_error("VMM Self-Test 6 Failed: Kernel higher-half section translation query failed!");
        return false;
    }
    klog_info(" [HARDWARE-VMM] Live CR3 PML4[0..255] verified unmapped (128 TiB, 0x0000000000000000 - 0x00007FFFFFFFFFFF).");

    // Test 7: Dynamic Page Allocation & Mapping
    PhysicalAddress test_frame = g_pmm.alloc_page();
    if (test_frame.is_null()) {
        klog_error("VMM Self-Test 7 Failed: PMM failed to allocate frame for mapping test!");
        return false;
    }

    VirtualAddress test_va(0xFFFFFFFF00000000ULL);
    VmmStatus map_res = map_page(test_va, test_frame, PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute);
    if (map_res != VmmStatus::Success) {
        klog_error("VMM Self-Test 7 Failed: map_page(%p -> %p) failed with code %d!",
                   test_va.value(), test_frame.value(), static_cast<int>(map_res));
        g_pmm.free_page(test_frame);
        return false;
    }

    // Test 8: Translation of newly mapped address
    PhysicalAddress trans_pa(0);
    PageFlags trans_flags = PageFlags::None;
    if (!translate(test_va, &trans_pa, &trans_flags)) {
        klog_error("VMM Self-Test 8 Failed: Newly mapped address %p was not found in page table walk!", test_va.value());
        unmap_page(test_va);
        g_pmm.free_page(test_frame);
        return false;
    }
    if (trans_pa != test_frame) {
        klog_error("VMM Self-Test 8 Failed: Translated address mismatch!");
        unmap_page(test_va);
        g_pmm.free_page(test_frame);
        return false;
    }
    if (!test_flag(trans_flags, PageFlags::Present) || !test_flag(trans_flags, PageFlags::Writable) || !test_flag(trans_flags, PageFlags::NoExecute)) {
        klog_error("VMM Self-Test 8 Failed: Mapped page flags mismatch!");
        unmap_page(test_va);
        g_pmm.free_page(test_frame);
        return false;
    }

    // Test 9: Data read/write through virtual mapping and cross-verification
    uint64_t* v_ptr = test_va.as<uint64_t>();
    constexpr uint64_t TEST_MAGIC1 = 0x12345678DEADBEEFULL;
    constexpr uint64_t TEST_MAGIC2 = 0xCAFEBABEBADF00D5ULL;
    v_ptr[0] = TEST_MAGIC1;
    v_ptr[511] = TEST_MAGIC2;

    uint64_t* p_ptr = phys_to_virt(test_frame).as<uint64_t>();
    if (p_ptr[0] != TEST_MAGIC1 || p_ptr[511] != TEST_MAGIC2) {
        klog_error("VMM Self-Test 9 Failed: Data written via virtual mapping did not appear in physical memory!");
        unmap_page(test_va);
        g_pmm.free_page(test_frame);
        return false;
    }

    // Test 10: Re-mapping already mapped virtual address must fail cleanly
    VmmStatus remap_res = map_page(test_va, test_frame, PageFlags::Present);
    if (remap_res != VmmStatus::AlreadyMapped) {
        klog_error("VMM Self-Test 10 Failed: Re-mapping existing address did not return AlreadyMapped!");
        unmap_page(test_va);
        g_pmm.free_page(test_frame);
        return false;
    }

    // Test 11: Unmapping test virtual address and verifying NotMapped
    VmmStatus unmap_res = unmap_page(test_va);
    if (unmap_res != VmmStatus::Success) {
        klog_error("VMM Self-Test 11 Failed: unmap_page(%p) failed!", test_va.value());
        g_pmm.free_page(test_frame);
        return false;
    }
    if (translate(test_va, &trans_pa, &trans_flags)) {
        klog_error("VMM Self-Test 11 Failed: Address still translates after unmap!");
        g_pmm.free_page(test_frame);
        return false;
    }
    if (unmap_page(test_va) != VmmStatus::NotMapped) {
        klog_error("VMM Self-Test 11 Failed: Unmapping free address did not return NotMapped!");
        g_pmm.free_page(test_frame);
        return false;
    }
    g_pmm.free_page(test_frame);

    // Test 12: Multi-page mapping and unmapping sequence
    constexpr size_t TEST_PAGE_COUNT = 4;
    PhysicalAddress multi_frames = g_pmm.alloc_pages(PageCount(TEST_PAGE_COUNT));
    if (multi_frames.is_null()) {
        klog_error("VMM Self-Test 12 Failed: Failed to allocate multi-page frames!");
        return false;
    }

    VirtualAddress multi_va_base(0xFFFFFFFF00010000ULL);
    for (size_t i = 0; i < TEST_PAGE_COUNT; ++i) {
        VirtualAddress cur_va = multi_va_base + (i * PAGE_SIZE);
        PhysicalAddress cur_pa = multi_frames + (i * PAGE_SIZE);
        if (map_page(cur_va, cur_pa, PageFlags::Present | PageFlags::Writable) != VmmStatus::Success) {
            klog_error("VMM Self-Test 12 Failed: Failed during multi-page mapping at index %llu!", i);
            return false;
        }
    }
    for (size_t i = 0; i < TEST_PAGE_COUNT; ++i) {
        VirtualAddress cur_va = multi_va_base + (i * PAGE_SIZE);
        PhysicalAddress cur_pa = multi_frames + (i * PAGE_SIZE);
        PhysicalAddress verify_pa;
        if (!translate(cur_va, &verify_pa) || verify_pa != cur_pa) {
            klog_error("VMM Self-Test 12 Failed: Translation mismatch for multi-page index %llu!", i);
            return false;
        }
    }
    for (size_t i = 0; i < TEST_PAGE_COUNT; ++i) {
        VirtualAddress cur_va = multi_va_base + (i * PAGE_SIZE);
        if (unmap_page(cur_va) != VmmStatus::Success) {
            klog_error("VMM Self-Test 12 Failed: Failed to unmap multi-page at index %llu!", i);
            return false;
        }
    }
    g_pmm.free_pages(multi_frames, PageCount(TEST_PAGE_COUNT));

    // Test 13: Controlled fault injection on page table allocation failure & invalid inputs [Gate 6]
    g_pmm.set_force_alloc_failure(true);
    VirtualAddress fault_va(0xFFFFFA0000000000ULL); // Canonical higher-half VA in unallocated PML4[500]
    VmmStatus fault_res = map_page(fault_va, PhysicalAddress(0x100000), PageFlags::Present);
    g_pmm.set_force_alloc_failure(false);
    if (fault_res != VmmStatus::OutOfMemory) {
        klog_error("VMM Self-Test 13 Failed: Allocation failure did not return OutOfMemory (got %d)!", static_cast<int>(fault_res));
        return false;
    }
    // Verify no partial page table was installed on allocation failure
    if (m_root_pml4->entries[fault_va.pml4_index()].is_present()) {
        klog_error("VMM Self-Test 13 Failed: Partial page table installed despite allocation failure!");
        return false;
    }

    // Verify VMM state restored after fault injection
    PhysicalAddress restore_frame = g_pmm.alloc_page();
    if (restore_frame.is_null()) {
        klog_error("VMM Self-Test 13 Failed: PMM allocation failed after clearing fault injection!");
        return false;
    }
    VirtualAddress restore_va(0xFFFFFFFF00020000ULL);
    if (map_page(restore_va, restore_frame, PageFlags::Present | PageFlags::Writable) != VmmStatus::Success) {
        klog_error("VMM Self-Test 13 Failed: map_page failed after fault injection recovery!");
        g_pmm.free_page(restore_frame);
        return false;
    }
    if (unmap_page(restore_va) != VmmStatus::Success) {
        klog_error("VMM Self-Test 13 Failed: unmap_page failed after fault injection recovery!");
        g_pmm.free_page(restore_frame);
        return false;
    }
    g_pmm.free_page(restore_frame);

    // Test invalid address inputs (verify rejection without installing partial page tables)
    VirtualAddress non_canonical_va(0x0008000000000000ULL);
    if (map_page(non_canonical_va, PhysicalAddress(0x100000), PageFlags::Present) != VmmStatus::InvalidVirtualAddress) {
        klog_error("VMM Self-Test 13 Failed: Non-canonical address was not rejected!");
        return false;
    }
    VirtualAddress unaligned_va(0xFFFFFFFF00000001ULL);
    if (map_page(unaligned_va, PhysicalAddress(0x100000), PageFlags::Present) != VmmStatus::UnalignedAddress) {
        klog_error("VMM Self-Test 13 Failed: Unaligned virtual address was not rejected!");
        return false;
    }
    PhysicalAddress dummy_pa;
    if (translate(unaligned_va.align_down(PAGE_SIZE), &dummy_pa)) {
        klog_error("VMM Self-Test 13 Failed: Partial page table installed after unaligned virtual mapping request!");
        return false;
    }
    if (map_page(VirtualAddress(0xFFFFFFFF00030000ULL), PhysicalAddress(0x100001), PageFlags::Present) != VmmStatus::UnalignedAddress) {
        klog_error("VMM Self-Test 13 Failed: Unaligned physical address was not rejected!");
        return false;
    }
    if (translate(VirtualAddress(0xFFFFFFFF00030000ULL), &dummy_pa)) {
        klog_error("VMM Self-Test 13 Failed: Partial page table installed after unaligned physical mapping request!");
        return false;
    }

    // Test 14: Operational Latency Benchmark via RDTSC [Gate 15]
    PhysicalAddress perf_frame = g_pmm.alloc_page();
    VirtualAddress perf_va(0xFFFFFFFF00040000ULL);

    uint64_t t_map_0 = arch::x86_64::rdtsc();
    map_page(perf_va, perf_frame, PageFlags::Present | PageFlags::Writable);
    uint64_t t_map_1 = arch::x86_64::rdtsc();

    PhysicalAddress perf_trans_pa;
    uint64_t t_trans_0 = arch::x86_64::rdtsc();
    bool trans_ok = translate(perf_va, &perf_trans_pa);
    uint64_t t_trans_1 = arch::x86_64::rdtsc();
    if (!trans_ok) {
        klog_error("VMM Self-Test 14 Failed: Translation failed during latency benchmark!");
        return false;
    }

    uint64_t t_unmap_0 = arch::x86_64::rdtsc();
    unmap_page(perf_va);
    uint64_t t_unmap_1 = arch::x86_64::rdtsc();

    g_pmm.free_page(perf_frame);

    klog_info(" [PERF] VMM end-to-end map_page latency   : %llu CPU cycles", t_map_1 - t_map_0);
    klog_info(" [PERF] VMM 4-level translate latency      : %llu CPU cycles", t_trans_1 - t_trans_0);
    klog_info(" [PERF] VMM end-to-end unmap_page latency : %llu CPU cycles", t_unmap_1 - t_unmap_0);

    klog_info("VMM validation self-test suite completed successfully (14/14 gates passed).");
    return true;
}

} // namespace llamaos::memory

