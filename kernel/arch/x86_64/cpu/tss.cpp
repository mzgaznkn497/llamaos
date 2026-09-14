#include "tss.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Task State Segment (TSS) and IST Implementation
// =============================================================================

namespace llamaos::arch::x86_64 {

TaskStateSegment* TssManager::s_tss{nullptr};
memory::PhysicalAddress TssManager::s_tss_paddr{0};
memory::PhysicalAddress TssManager::s_ist1_paddr{0};
memory::PhysicalAddress TssManager::s_ist2_paddr{0};
memory::PhysicalAddress TssManager::s_ist3_paddr{0};

bool TssManager::init(uint64_t kernel_rsp0) {
    using namespace memory;

    // 1. Allocate dedicated physical memory frames via PMM
    s_ist1_paddr = g_pmm.alloc_pages(PageCount(IST_STACK_PAGES));
    if (s_ist1_paddr.is_null()) {
        klog_error("TSS Init: Failed to allocate physical memory for IST1 (#DF stack)!");
        return false;
    }

    s_ist2_paddr = g_pmm.alloc_pages(PageCount(IST_STACK_PAGES));
    if (s_ist2_paddr.is_null()) {
        klog_error("TSS Init: Failed to allocate physical memory for IST2 (#PF stack)!");
        return false;
    }

    s_ist3_paddr = g_pmm.alloc_pages(PageCount(IST_STACK_PAGES));
    if (s_ist3_paddr.is_null()) {
        klog_error("TSS Init: Failed to allocate physical memory for IST3 (Critical stack)!");
        return false;
    }

    s_tss_paddr = g_pmm.alloc_page();
    if (s_tss_paddr.is_null()) {
        klog_error("TSS Init: Failed to allocate physical memory for TSS structure!");
        return false;
    }

    // 2. Map dedicated IST stacks into the active VMM page tables
    // Enforce RW NX permissions (Supervisor only, No-Execute, Writable)
    PageFlags stack_flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;

    for (size_t i = 0; i < IST_STACK_PAGES; ++i) {
        VirtualAddress va1 = IST1_STACK_BOTTOM + (i * PAGE_SIZE);
        PhysicalAddress pa1 = s_ist1_paddr + (i * PAGE_SIZE);
        if (g_vmm.map_page(va1, pa1, stack_flags) != VmmStatus::Success) {
            klog_error("TSS Init: Failed to map virtual page %p for IST1!", va1.value());
            return false;
        }

        VirtualAddress va2 = IST2_STACK_BOTTOM + (i * PAGE_SIZE);
        PhysicalAddress pa2 = s_ist2_paddr + (i * PAGE_SIZE);
        if (g_vmm.map_page(va2, pa2, stack_flags) != VmmStatus::Success) {
            klog_error("TSS Init: Failed to map virtual page %p for IST2!", va2.value());
            return false;
        }

        VirtualAddress va3 = IST3_STACK_BOTTOM + (i * PAGE_SIZE);
        PhysicalAddress pa3 = s_ist3_paddr + (i * PAGE_SIZE);
        if (g_vmm.map_page(va3, pa3, stack_flags) != VmmStatus::Success) {
            klog_error("TSS Init: Failed to map virtual page %p for IST3!", va3.value());
            return false;
        }
    }

    // 3. Map TSS page into active VMM
    if (g_vmm.map_page(TSS_VIRTUAL_ADDR, s_tss_paddr, stack_flags) != VmmStatus::Success) {
        klog_error("TSS Init: Failed to map virtual page for TSS at %p!", TSS_VIRTUAL_ADDR.value());
        return false;
    }

    // Note: Guard pages IST1_GUARD_VIRTUAL, IST2_GUARD_VIRTUAL, IST3_GUARD_VIRTUAL,
    // and IST4_GUARD_VIRTUAL remain strictly unmapped in VMM to catch overflows.

    // 4. Initialize TSS fields
    s_tss = TSS_VIRTUAL_ADDR.as<TaskStateSegment>();
    memset(s_tss, 0, sizeof(TaskStateSegment));

    s_tss->rsp0 = kernel_rsp0;
    s_tss->ist1 = IST1_STACK_TOP.value();
    s_tss->ist2 = IST2_STACK_TOP.value();
    s_tss->ist3 = IST3_STACK_TOP.value();
    s_tss->iomap_base = sizeof(TaskStateSegment);

    klog_info("TSS Initialized successfully:");
    klog_info("  TSS Structure : VA=%p, PA=%p (size: %u bytes)",
              TSS_VIRTUAL_ADDR.value(), s_tss_paddr.value(), static_cast<uint32_t>(sizeof(TaskStateSegment)));
    klog_info("  RSP0 Base     : %p", s_tss->rsp0);
    klog_info("  IST1 (#DF)    : Top=%p, Bottom=%p (16 KiB, PA=%p)",
              s_tss->ist1, IST1_STACK_BOTTOM.value(), s_ist1_paddr.value());
    klog_info("  IST2 (#PF)    : Top=%p, Bottom=%p (16 KiB, PA=%p)",
              s_tss->ist2, IST2_STACK_BOTTOM.value(), s_ist2_paddr.value());
    klog_info("  IST3 (Crit)   : Top=%p, Bottom=%p (16 KiB, PA=%p)",
              s_tss->ist3, IST3_STACK_BOTTOM.value(), s_ist3_paddr.value());

    return true;
}

bool TssManager::verify() {
    using namespace memory;

    if (!s_tss) {
        klog_error("TSS Verify Failed: TSS pointer is null!");
        return false;
    }

    // 1. Numerical checks
    if (s_tss->ist1 != IST1_STACK_TOP.value() ||
        s_tss->ist2 != IST2_STACK_TOP.value() ||
        s_tss->ist3 != IST3_STACK_TOP.value()) {
        klog_error("TSS Verify Failed: IST top addresses do not match configuration!");
        return false;
    }

    // 16-byte stack alignment check
    if ((s_tss->ist1 & 0xFULL) != 0 ||
        (s_tss->ist2 & 0xFULL) != 0 ||
        (s_tss->ist3 & 0xFULL) != 0) {
        klog_error("TSS Verify Failed: IST stacks are not 16-byte aligned!");
        return false;
    }

    // Distinct stack check
    if (s_tss->ist1 == s_tss->ist2 || s_tss->ist2 == s_tss->ist3 || s_tss->ist1 == s_tss->ist3) {
        klog_error("TSS Verify Failed: Shared IST stack detected!");
        return false;
    }

    // 2. Hardware VMM page table mapping verification
    PhysicalAddress q_pa;
    PageFlags q_flags;

    // TSS mapping check
    if (!g_vmm.translate(TSS_VIRTUAL_ADDR, &q_pa, &q_flags)) {
        klog_error("TSS Verify Failed: TSS virtual address %p is not mapped in VMM!", TSS_VIRTUAL_ADDR.value());
        return false;
    }
    if (q_pa != s_tss_paddr) {
        klog_error("TSS Verify Failed: TSS physical address translation mismatch (%p != %p)!",
                   q_pa.value(), s_tss_paddr.value());
        return false;
    }
    if (!test_flag(q_flags, PageFlags::Present) ||
        !test_flag(q_flags, PageFlags::Writable) ||
        !test_flag(q_flags, PageFlags::NoExecute) ||
        test_flag(q_flags, PageFlags::User)) {
        klog_error("TSS Verify Failed: TSS page flags invalid (must be RW NX Kernel)!");
        return false;
    }

    // IST stack pages mapping checks
    auto verify_stack_range = [](VirtualAddress bottom, PhysicalAddress base_paddr, const char* name) -> bool {
        for (size_t i = 0; i < IST_STACK_PAGES; ++i) {
            VirtualAddress va = bottom + (i * PAGE_SIZE);
            PhysicalAddress pa;
            PageFlags flags;
            if (!g_vmm.translate(va, &pa, &flags)) {
                klog_error("TSS Verify Failed: %s page %u at %p not mapped!", name, static_cast<uint32_t>(i), va.value());
                return false;
            }
            if (pa != base_paddr + (i * PAGE_SIZE)) {
                klog_error("TSS Verify Failed: %s page %u physical address mismatch!", name, static_cast<uint32_t>(i));
                return false;
            }
            if (!test_flag(flags, PageFlags::Present) ||
                !test_flag(flags, PageFlags::Writable) ||
                !test_flag(flags, PageFlags::NoExecute) ||
                test_flag(flags, PageFlags::User)) {
                klog_error("TSS Verify Failed: %s page %u flags invalid (must be RW NX Kernel)!", name, static_cast<uint32_t>(i));
                return false;
            }
        }
        return true;
    };

    if (!verify_stack_range(IST1_STACK_BOTTOM, s_ist1_paddr, "IST1") ||
        !verify_stack_range(IST2_STACK_BOTTOM, s_ist2_paddr, "IST2") ||
        !verify_stack_range(IST3_STACK_BOTTOM, s_ist3_paddr, "IST3")) {
        return false;
    }

    // 3. Guard page non-present verification
    if (g_vmm.translate(IST1_GUARD_VIRTUAL, &q_pa, &q_flags) ||
        g_vmm.translate(IST2_GUARD_VIRTUAL, &q_pa, &q_flags) ||
        g_vmm.translate(IST3_GUARD_VIRTUAL, &q_pa, &q_flags) ||
        g_vmm.translate(IST4_GUARD_VIRTUAL, &q_pa, &q_flags)) {
        klog_error("TSS Verify Failed: One or more guard pages are unexpectedly mapped!");
        return false;
    }

    return true;
}

} // namespace llamaos::arch::x86_64
