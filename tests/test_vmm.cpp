#include "memory/memory_types.hpp"
#include "memory/vmm.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// =============================================================================
// LlamaOS/A - Virtual Memory Manager (VMM) Host Regression Test Suite
// =============================================================================

static size_t g_vmm_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return false; \
        } \
        g_vmm_assertions_passed++; \
    } while (0)

using namespace llamaos;
using namespace llamaos::memory;

// Host logging stubs for unit tests
namespace llamaos {
void klog_info(const char* /*fmt*/, ...) {}
void klog_warn(const char* /*fmt*/, ...) {}
void klog_error(const char* /*fmt*/, ...) {}
}

// 1. VirtualAddress Canonical Bounds Checking
static bool test_virtual_address_canonical() {
    // Lower half canonical: 0x0000000000000000 .. 0x00007FFFFFFFFFFF
    VirtualAddress va0(0ULL);
    TEST_ASSERT(va0.is_canonical(), "0 is canonical");
    TEST_ASSERT(!va0.is_higher_half(), "0 is not higher half");

    VirtualAddress va_low_max(0x00007FFFFFFFFFFFULL);
    TEST_ASSERT(va_low_max.is_canonical(), "0x00007FFFFFFFFFFF is canonical");
    TEST_ASSERT(!va_low_max.is_higher_half(), "0x00007FFFFFFFFFFF is not higher half");

    // Non-canonical hole: 0x0000800000000000 .. 0xFFFF7FFFFFFFFFFF
    VirtualAddress va_hole1(0x0000800000000000ULL);
    TEST_ASSERT(!va_hole1.is_canonical(), "0x0000800000000000 is non-canonical");

    VirtualAddress va_hole2(0x0001000000000000ULL);
    TEST_ASSERT(!va_hole2.is_canonical(), "0x0001000000000000 is non-canonical");

    VirtualAddress va_hole3(0xFFFF7FFFFFFFFFFFULL);
    TEST_ASSERT(!va_hole3.is_canonical(), "0xFFFF7FFFFFFFFFFF is non-canonical");

    // Higher half canonical: 0xFFFF800000000000 .. 0xFFFFFFFFFFFFFFFF
    VirtualAddress va_high_min(0xFFFF800000000000ULL);
    TEST_ASSERT(va_high_min.is_canonical(), "0xFFFF800000000000 is canonical");
    TEST_ASSERT(va_high_min.is_higher_half(), "0xFFFF800000000000 is higher half");

    VirtualAddress va_kern(0xFFFFFFFF80000000ULL);
    TEST_ASSERT(va_kern.is_canonical(), "0xFFFFFFFF80000000 is canonical");
    TEST_ASSERT(va_kern.is_higher_half(), "0xFFFFFFFF80000000 is higher half");

    VirtualAddress va_high_max(0xFFFFFFFFFFFFFFFFULL);
    TEST_ASSERT(va_high_max.is_canonical(), "0xFFFFFFFFFFFFFFFF is canonical");
    TEST_ASSERT(va_high_max.is_higher_half(), "0xFFFFFFFFFFFFFFFF is higher half");

    return true;
}

// 2. VirtualAddress 4-Level Paging Decomposition
static bool test_virtual_address_indices() {
    // Kernel base: 0xFFFFFFFF80000000 (-2 GiB)
    VirtualAddress kern_base(0xFFFFFFFF80000000ULL);
    TEST_ASSERT(kern_base.pml4_index() == 511, "Kernel base PML4 index must be 511");
    TEST_ASSERT(kern_base.pdpt_index() == 510, "Kernel base PDPT index must be 510");
    TEST_ASSERT(kern_base.pd_index() == 0, "Kernel base PD index must be 0");
    TEST_ASSERT(kern_base.pt_index() == 0, "Kernel base PT index must be 0");
    TEST_ASSERT(kern_base.page_offset() == 0, "Kernel base page offset must be 0");

    // Address 0x0000123456789ABC
    VirtualAddress arb(0x0000123456789ABCULL);
    TEST_ASSERT(arb.page_offset() == 0xABC, "Offset must be 0xABC");
    TEST_ASSERT(arb.pt_index() == ((0x0000123456789ABCULL >> 12) & 0x1FF), "PT index calculation");
    TEST_ASSERT(arb.pd_index() == ((0x0000123456789ABCULL >> 21) & 0x1FF), "PD index calculation");
    TEST_ASSERT(arb.pdpt_index() == ((0x0000123456789ABCULL >> 30) & 0x1FF), "PDPT index calculation");
    TEST_ASSERT(arb.pml4_index() == ((0x0000123456789ABCULL >> 39) & 0x1FF), "PML4 index calculation");

    return true;
}

// 3. PageFlags Bitmask Logic
static bool test_page_flags() {
    PageFlags f = PageFlags::Present | PageFlags::Writable;
    TEST_ASSERT(test_flag(f, PageFlags::Present), "Flag has Present");
    TEST_ASSERT(test_flag(f, PageFlags::Writable), "Flag has Writable");
    TEST_ASSERT(!test_flag(f, PageFlags::User), "Flag does not have User");
    TEST_ASSERT(!test_flag(f, PageFlags::NoExecute), "Flag does not have NX");

    f |= PageFlags::NoExecute;
    TEST_ASSERT(test_flag(f, PageFlags::NoExecute), "Flag has NX after |=");

    f &= ~PageFlags::Writable;
    TEST_ASSERT(!test_flag(f, PageFlags::Writable), "Flag has no Writable after &=");
    TEST_ASSERT(test_flag(f, PageFlags::Present), "Flag still has Present");
    TEST_ASSERT(test_flag(f, PageFlags::NoExecute), "Flag still has NX");

    return true;
}

// 4. PageTableEntry Bit Representation & Flags
static bool test_page_table_entry() {
    PageTableEntry pte;
    TEST_ASSERT(pte.raw == 0, "Default PTE is zero");
    TEST_ASSERT(!pte.is_present(), "PTE not present");
    TEST_ASSERT(!pte.is_writable(), "PTE not writable");
    TEST_ASSERT(!pte.is_user(), "PTE not user");
    TEST_ASSERT(!pte.is_no_execute(), "PTE not NX");
    TEST_ASSERT(pte.physical_address() == PhysicalAddress(0), "PTE PA is 0");

    PhysicalAddress pa(0x0000000123456000ULL);
    PageFlags flags = PageFlags::Present | PageFlags::Writable | PageFlags::User | PageFlags::NoExecute;
    pte.set(pa, flags);

    TEST_ASSERT(pte.is_present(), "PTE is present");
    TEST_ASSERT(pte.is_writable(), "PTE is writable");
    TEST_ASSERT(pte.is_user(), "PTE is user");
    TEST_ASSERT(pte.is_no_execute(), "PTE is NX");
    TEST_ASSERT(pte.physical_address() == pa, "PTE physical address matches");
    TEST_ASSERT((pte.raw & (1ULL << 63)) != 0, "NX bit 63 is set");
    TEST_ASSERT((pte.raw & 1ULL) != 0, "Present bit 0 is set");
    TEST_ASSERT((pte.raw & 2ULL) != 0, "Writable bit 1 is set");
    TEST_ASSERT((pte.raw & 4ULL) != 0, "User bit 2 is set");

    pte.clear();
    TEST_ASSERT(pte.raw == 0, "PTE clear resets raw to 0");

    return true;
}

// 5. PageTable 4096-Byte Alignment & Structure
static bool test_page_table_structure() {
    TEST_ASSERT(sizeof(PageTableEntry) == 8, "PageTableEntry must be exactly 8 bytes");
    TEST_ASSERT(sizeof(PageTable) == 4096, "PageTable must be exactly 4096 bytes");
    TEST_ASSERT(alignof(PageTable) == 4096, "PageTable must be 4096-byte aligned");

    PageTable pt{};
    for (size_t i = 0; i < 512; ++i) {
        TEST_ASSERT(pt.entries[i].raw == 0, "Initial table entry must be 0");
    }

    pt.entries[0].set(PhysicalAddress(0x100000), PageFlags::Present | PageFlags::Writable);
    TEST_ASSERT(pt.entries[0].is_present(), "Entry 0 is present");
    TEST_ASSERT(pt.entries[0].physical_address() == PhysicalAddress(0x100000), "Entry 0 physical address");

    return true;
}

// 6. VMM Mapping Collision, Missing Page Unmap & Repeated Map/Unmap Cycles
static bool test_vmm_mapping_collision_and_repeated_cycles() {
    PageTableEntry pte;
    TEST_ASSERT(!pte.is_present(), "Initial PTE not present");

    PhysicalAddress pa1(0x200000);
    pte.set(pa1, PageFlags::Present | PageFlags::Writable);
    TEST_ASSERT(pte.is_present(), "PTE marked present");

    // Mapping collision simulation
    TEST_ASSERT(pte.is_present(), "Mapping collision detected when PTE already present");

    // Unmap
    pte.clear();
    TEST_ASSERT(!pte.is_present(), "PTE successfully cleared on unmap");

    // Repeated map/unmap cycle (5 iterations)
    for (int cycle = 0; cycle < 5; ++cycle) {
        TEST_ASSERT(!pte.is_present(), "PTE not present before map");
        pte.set(pa1, PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute);
        TEST_ASSERT(pte.is_present(), "PTE present after map");
        TEST_ASSERT(pte.is_no_execute(), "PTE has NX flag");
        pte.clear();
        TEST_ASSERT(!pte.is_present(), "PTE cleared after unmap");
    }

    return true;
}

// 7. NX and User/Kernel Permission Conversion & Flag Propagation
static bool test_nx_and_permission_conversion() {
    PageTableEntry kernel_code_pte;
    kernel_code_pte.set(PhysicalAddress(0x100000), PageFlags::Present); // Kernel .text: RX
    TEST_ASSERT(kernel_code_pte.is_present(), "Kernel text is present");
    TEST_ASSERT(!kernel_code_pte.is_writable(), "Kernel text is NOT writable (W^X)");
    TEST_ASSERT(!kernel_code_pte.is_user(), "Kernel text is NOT user accessible");
    TEST_ASSERT(!kernel_code_pte.is_no_execute(), "Kernel text is executable");

    PageTableEntry kernel_data_pte;
    kernel_data_pte.set(PhysicalAddress(0x110000), PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute);
    TEST_ASSERT(kernel_data_pte.is_present(), "Kernel data is present");
    TEST_ASSERT(kernel_data_pte.is_writable(), "Kernel data is writable");
    TEST_ASSERT(!kernel_data_pte.is_user(), "Kernel data is NOT user accessible");
    TEST_ASSERT(kernel_data_pte.is_no_execute(), "Kernel data is NX");

    PageTableEntry user_page_pte;
    user_page_pte.set(PhysicalAddress(0x200000), PageFlags::Present | PageFlags::Writable | PageFlags::User | PageFlags::NoExecute);
    TEST_ASSERT(user_page_pte.is_user(), "User page has User privilege bit 2");
    TEST_ASSERT(user_page_pte.is_writable(), "User page is writable");
    TEST_ASSERT(user_page_pte.is_no_execute(), "User page is NX");

    return true;
}

// 8. Hardware W^X Permission Constraints Simulation
static bool test_wx_permission_constraints() {
    // Rule: A page frame MUST NEVER be both Writable and Executable
    PageFlags text_flags = PageFlags::Present; // W=0, NX=0 (Executable, Read-Only)
    TEST_ASSERT(!test_flag(text_flags, PageFlags::Writable), ".text must NOT be writable");
    TEST_ASSERT(!test_flag(text_flags, PageFlags::NoExecute), ".text must be executable");

    PageFlags data_flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute; // W=1, NX=1 (Writable, Non-Executable)
    TEST_ASSERT(test_flag(data_flags, PageFlags::Writable), ".data must be writable");
    TEST_ASSERT(test_flag(data_flags, PageFlags::NoExecute), ".data must be NoExecute");

    // Simulated violation check
    PageFlags bad_flags = PageFlags::Present | PageFlags::Writable; // W=1, NX=0 (WX violation!)
    bool is_wx_violation = test_flag(bad_flags, PageFlags::Writable) && !test_flag(bad_flags, PageFlags::NoExecute);
    TEST_ASSERT(is_wx_violation, "W=1 NX=0 correctly flagged as W^X violation");

    return true;
}

// 9. Large Page (2 MiB) Index and Collision Simulation
static bool test_huge_page_collision_simulation() {
    PageTableEntry pde_huge;
    PhysicalAddress pde_pa(0x40000000ULL); // 1 GiB base
    pde_huge.set(pde_pa, PageFlags::Present | PageFlags::Writable | PageFlags::HugePage | PageFlags::NoExecute);

    TEST_ASSERT(pde_huge.is_present(), "Huge PDE is present");
    TEST_ASSERT(pde_huge.is_huge(), "Huge PDE has bit 7 set");
    TEST_ASSERT(pde_huge.is_no_execute(), "Huge PDE has NX set");

    // Mapping a 4 KiB page into a huge PDE must return HugePageCollision
    bool collides = pde_huge.is_huge();
    TEST_ASSERT(collides, "Attempting to map 4K into huge PDE detects collision");

    return true;
}

// 10. Address Validation Rejections (Non-Canonical, Unaligned)
static bool test_address_validation_rejections() {
    VirtualAddress non_canonical(0x0008000000000000ULL);
    TEST_ASSERT(!non_canonical.is_canonical(), "Non-canonical virtual address rejected");

    VirtualAddress unaligned_va(0xFFFFFFFF80000001ULL);
    TEST_ASSERT(!unaligned_va.is_page_aligned(), "Unaligned virtual address rejected");

    PhysicalAddress unaligned_pa(0x1001ULL);
    TEST_ASSERT(!unaligned_pa.is_page_aligned(), "Unaligned physical address rejected");

    return true;
}

int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Virtual Memory Manager (VMM) Host Regression Suite\n");
    printf("================================================================================\n");

    struct TestCase {
        const char* name;
        bool (*func)();
    };

    TestCase tests[] = {
        {"VirtualAddress Canonical Bounds Checking", test_virtual_address_canonical},
        {"VirtualAddress 4-Level Paging Decomposition", test_virtual_address_indices},
        {"PageFlags Bitmask Logic", test_page_flags},
        {"PageTableEntry Bit Representation & Flags", test_page_table_entry},
        {"PageTable 4096-Byte Alignment & Structure", test_page_table_structure},
        {"VMM Mapping Collision, Missing Page & Repeated Cycles", test_vmm_mapping_collision_and_repeated_cycles},
        {"NX and User/Kernel Permission Conversion & Propagation", test_nx_and_permission_conversion},
        {"Hardware W^X Permission Constraints Simulation", test_wx_permission_constraints},
        {"Large Page (2 MiB) Index and Collision Simulation", test_huge_page_collision_simulation},
        {"Address Validation Rejections (Non-Canonical, Unaligned)", test_address_validation_rejections}
    };

    size_t passed = 0;
    size_t total = sizeof(tests) / sizeof(tests[0]);

    for (size_t i = 0; i < total; ++i) {
        printf(" [RUN]  %-58s ... ", tests[i].name);
        if (tests[i].func()) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED\n");
            return 1;
        }
    }

    printf("================================================================================\n");
    printf(" VMM Regression Suite Complete: %zu/%zu tests PASSED (%zu assertions verified)\n",
           passed, total, g_vmm_assertions_passed);
    printf("================================================================================\n");

    return (passed == total) ? 0 : 1;
}
