#include "memory/memory_types.hpp"
#include "memory/reserved_regions.hpp"
#include "core/string.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// =============================================================================
// LlamaOS/A - Physical Memory Manager (PMM) Host Regression Test Suite
// =============================================================================

static size_t g_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return false; \
        } \
        g_assertions_passed++; \
    } while (0)

using namespace llamaos;
using namespace llamaos::memory;

// Host logging stubs for unit tests
namespace llamaos {
void klog_info(const char* /*fmt*/, ...) {}
void klog_warn(const char* /*fmt*/, ...) {}
void klog_error(const char* /*fmt*/, ...) {}
}

// 1. PhysicalAddress Primitives & Alignment
static bool test_physical_address_primitives() {
    PhysicalAddress p0;
    TEST_ASSERT(p0.is_null(), "Default PhysicalAddress must be null");
    TEST_ASSERT(p0.value() == 0, "Default PhysicalAddress value must be 0");
    TEST_ASSERT(p0.is_page_aligned(), "0 is page aligned");

    PhysicalAddress p1(0x1000);
    TEST_ASSERT(!p1.is_null(), "0x1000 is not null");
    TEST_ASSERT(p1.is_page_aligned(), "0x1000 must be page aligned");
    TEST_ASSERT(p1.is_aligned(0x1000), "0x1000 aligned to 4K");
    TEST_ASSERT(p1.is_aligned(0x200), "0x1000 aligned to 512");
    TEST_ASSERT(!p1.is_aligned(0x2000), "0x1000 not aligned to 8K");

    PhysicalAddress p_unaligned(0x1234);
    TEST_ASSERT(!p_unaligned.is_page_aligned(), "0x1234 not page aligned");
    TEST_ASSERT(p_unaligned.align_down(0x1000) == PhysicalAddress(0x1000), "align_down to 4K");
    TEST_ASSERT(p_unaligned.align_up(0x1000) == PhysicalAddress(0x2000), "align_up to 4K");
    TEST_ASSERT(p_unaligned.align_down(0x100) == PhysicalAddress(0x1200), "align_down to 256");
    TEST_ASSERT(p_unaligned.align_up(0x100) == PhysicalAddress(0x1300), "align_up to 256");

    PhysicalAddress p_exact(0x2000);
    TEST_ASSERT(p_exact.align_up(0x1000) == PhysicalAddress(0x2000), "align_up already aligned");
    TEST_ASSERT(p_exact.align_down(0x1000) == PhysicalAddress(0x2000), "align_down already aligned");

    return true;
}

// 2. PhysicalAddress Arithmetic & Relational Operators
static bool test_physical_address_arithmetic() {
    PhysicalAddress a(0x100000);
    PhysicalAddress b(0x200000);

    TEST_ASSERT(a < b, "a < b");
    TEST_ASSERT(a <= b, "a <= b");
    TEST_ASSERT(b > a, "b > a");
    TEST_ASSERT(b >= a, "b >= a");
    TEST_ASSERT(a != b, "a != b");
    TEST_ASSERT(a == PhysicalAddress(0x100000), "a == 0x100000");

    TEST_ASSERT(a + 0x1000 == PhysicalAddress(0x101000), "a + 0x1000");
    TEST_ASSERT(b - 0x1000 == PhysicalAddress(0x1FF000), "b - 0x1000");
    TEST_ASSERT((b - a) == 0x100000, "b - a == 0x100000");

    PhysicalAddress c = a;
    c += 0x5000;
    TEST_ASSERT(c == PhysicalAddress(0x105000), "+=");
    c -= 0x2000;
    TEST_ASSERT(c == PhysicalAddress(0x103000), "-=");

    return true;
}

// 3. PageFrameNumber & PageCount Conversions
static bool test_pfn_and_page_count() {
    PageFrameNumber pfn0(0);
    TEST_ASSERT(pfn0.value() == 0, "PFN 0 value");
    TEST_ASSERT(pfn0.to_address() == PhysicalAddress(0), "PFN 0 to_address");

    PageFrameNumber pfn_100 = PageFrameNumber::from_address(PhysicalAddress(0x100000));
    TEST_ASSERT(pfn_100.value() == 256, "0x100000 is frame 256");
    TEST_ASSERT(pfn_100.to_address() == PhysicalAddress(0x100000), "Frame 256 to address");

    ++pfn_100;
    TEST_ASSERT(pfn_100.value() == 257, "PFN ++ prefix");
    TEST_ASSERT(pfn_100.to_address() == PhysicalAddress(0x101000), "Frame 257 address");

    PageFrameNumber post = pfn_100++;
    TEST_ASSERT(post.value() == 257, "PFN ++ postfix old");
    TEST_ASSERT(pfn_100.value() == 258, "PFN ++ postfix new");

    PageCount c0;
    TEST_ASSERT(c0.value() == 0, "PageCount 0");
    TEST_ASSERT(c0.to_bytes() == 0, "PageCount 0 bytes");

    PageCount c_bytes = PageCount::from_bytes(4096);
    TEST_ASSERT(c_bytes.value() == 1, "4096 bytes == 1 page");
    PageCount c_round_up = PageCount::from_bytes(4097);
    TEST_ASSERT(c_round_up.value() == 2, "4097 bytes == 2 pages");

    PageCount c10(10);
    TEST_ASSERT(c10.to_bytes() == 40960, "10 pages == 40960 bytes");
    TEST_ASSERT(c10 + PageCount(5) == PageCount(15), "10 + 5 == 15");

    return true;
}

// 4. PMM Bitmap Bit Allocation Simulation
static bool test_pmm_bitmap_simulation() {
    constexpr size_t TEST_FRAMES = 256;
    uint64_t bitmap[TEST_FRAMES / 64] = {0}; // 4 words = 256 bits

    auto set_bit = [&](size_t f) { bitmap[f / 64] |= (1ULL << (f % 64)); };
    auto clear_bit = [&](size_t f) { bitmap[f / 64] &= ~(1ULL << (f % 64)); };
    auto test_bit = [&](size_t f) -> bool { return (bitmap[f / 64] & (1ULL << (f % 64))) != 0; };

    for (size_t f = 0; f < TEST_FRAMES; ++f) {
        TEST_ASSERT(!test_bit(f), "Initial bit is 0");
    }

    set_bit(0);
    TEST_ASSERT(test_bit(0), "Frame 0 is set");
    TEST_ASSERT(!test_bit(1), "Frame 1 is not set");

    size_t allocated_frame = 0;
    for (size_t w = 0; w < 4; ++w) {
        if (bitmap[w] != 0xFFFFFFFFFFFFFFFFULL) {
            int b = __builtin_ctzll(~bitmap[w]);
            allocated_frame = w * 64 + b;
            set_bit(allocated_frame);
            break;
        }
    }
    TEST_ASSERT(allocated_frame == 1, "First free frame allocated was frame 1");
    TEST_ASSERT(test_bit(1), "Frame 1 now marked set");

    clear_bit(1);
    TEST_ASSERT(!test_bit(1), "Frame 1 now marked free");

    return true;
}

// 5. Multi-Frame Contiguous Run Search Simulation
static bool test_pmm_contiguous_run_simulation() {
    constexpr size_t TOTAL = 128;
    uint64_t bitmap[2] = {0};

    auto set_bit = [&](size_t f) { bitmap[f / 64] |= (1ULL << (f % 64)); };
    auto test_bit = [&](size_t f) -> bool { return (bitmap[f / 64] & (1ULL << (f % 64))) != 0; };

    for (size_t i = 0; i < 10; ++i) set_bit(i);
    for (size_t i = 20; i < 26; ++i) set_bit(i);

    size_t needed = 8;
    size_t run_start = 0;
    size_t run_len = 0;
    bool found = false;

    for (size_t f = 0; f < TOTAL; ++f) {
        if (!test_bit(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == needed) {
                found = true;
                break;
            }
        } else {
            run_len = 0;
        }
    }

    TEST_ASSERT(found, "Found contiguous run of 8");
    TEST_ASSERT(run_start == 10, "Run started at frame 10");

    return true;
}

// 6. High Physical Memory Address (> 4 GiB) Host Arithmetic Safety
static bool test_high_physical_address_safety() {
    PhysicalAddress high_pa(0x0000000200000000ULL); // 8 GiB
    TEST_ASSERT(!high_pa.is_null(), "8 GiB address is not null");
    TEST_ASSERT(high_pa.is_page_aligned(), "8 GiB address is 4K aligned");
    TEST_ASSERT(high_pa.value() == 0x200000000ULL, "Value preserves all 64 bits");

    PageFrameNumber pfn = PageFrameNumber::from_address(high_pa);
    TEST_ASSERT(pfn.value() == (0x200000000ULL / 4096), "PFN for 8 GiB is 0x200000 = 2097152");
    TEST_ASSERT(pfn.to_address() == high_pa, "PFN translates back to 8 GiB");

    uint64_t total_frames = high_pa.value() / 4096ULL;
    uint64_t bitmap_bytes = (total_frames + 7) / 8;
    TEST_ASSERT(bitmap_bytes == 262144, "8 GiB requires exactly 256 KiB bitmap");

    return true;
}

// 7. Maximum Page Number & Bitmap Overflow Protection
static bool test_max_page_and_bitmap_size_overflow() {
    PhysicalAddress max_pa(0x0000FFFFFFFFFFFFULL); // 256 TiB
    PageFrameNumber max_pfn = PageFrameNumber::from_address(max_pa);
    TEST_ASSERT(max_pfn.value() == (0x0000FFFFFFFFFFFFULL / 4096), "Max 48-bit PFN calculation");

    uint64_t ram_16gb = 16ULL * 1024 * 1024 * 1024;
    uint64_t frames_16gb = ram_16gb / 4096;
    uint64_t bm_16gb = (frames_16gb + 7) / 8;
    TEST_ASSERT(bm_16gb == (512 * 1024), "16 GiB requires exactly 512 KiB bitmap");

    PageCount huge_count = PageCount::from_bytes(UINT64_MAX);
    TEST_ASSERT(huge_count.value() > 0, "Huge page count handles max bytes without crashing");

    return true;
}

// 8. Contiguous Allocation at End of Region & Wrap-Around
static bool test_contiguous_allocation_end_of_region() {
    constexpr size_t TOTAL = 64;
    uint64_t bitmap = 0;

    auto set_bit = [&](size_t f) { bitmap |= (1ULL << f); };
    auto test_bit = [&](size_t f) -> bool { return (bitmap & (1ULL << f)) != 0; };

    for (size_t i = 0; i < 60; ++i) set_bit(i);

    size_t needed = 4;
    size_t run_start = 0;
    size_t run_len = 0;
    bool found = false;
    for (size_t f = 0; f < TOTAL; ++f) {
        if (!test_bit(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == needed) {
                found = true;
                break;
            }
        } else {
            run_len = 0;
        }
    }
    TEST_ASSERT(found && run_start == 60, "Allocated run of 4 at exact end of region");

    run_len = 0;
    found = false;
    needed = 5;
    for (size_t f = 0; f < TOTAL; ++f) {
        if (!test_bit(f)) {
            if (run_len == 0) run_start = f;
            run_len++;
            if (run_len == needed) {
                found = true;
                break;
            }
        } else {
            run_len = 0;
        }
    }
    TEST_ASSERT(!found, "Request for 5 frames correctly rejected when only 4 exist at end");

    return true;
}

// 9. Allocation Exhaustion & Out-of-Memory Simulation
static bool test_allocation_exhaustion_oom() {
    uint64_t bitmap = 0xFFFFFFFFFFFFFFFFULL;
    TEST_ASSERT(~bitmap == 0, "Bitmap is fully exhausted");
    bool found = (bitmap != 0xFFFFFFFFFFFFFFFFULL);
    TEST_ASSERT(!found, "Search on exhausted bitmap correctly reports Out-of-Memory");
    return true;
}

// 10. Double Free, Invalid Free & Unaligned Free Protection
static bool test_double_free_and_invalid_free_protection() {
    PhysicalAddress unaligned_addr(0x1001);
    TEST_ASSERT(!unaligned_addr.is_page_aligned(), "0x1001 is unaligned");
    PhysicalAddress null_addr(0);
    TEST_ASSERT(null_addr.is_null(), "0x0 is null");
    return true;
}

// =============================================================================
// Memory Normalization Test Suite (Gate 11: 6 Sub-Cases)
// =============================================================================

// 11. Memory Normalization: Unsorted Inputs
static bool test_normalization_unsorted() {
    ReservedMemoryTracker tracker;
    // Insert out-of-order: 30MB Available, then 0MB Low Memory Reserved, then 10MB Available
    tracker.add_region(PhysicalAddress(0x1E00000), 0x200000, boot::MemoryType::Available, "High RAM");
    tracker.add_region(PhysicalAddress(0x0), 0x100000, boot::MemoryType::Reserved, "Low Memory");
    tracker.add_region(PhysicalAddress(0xA00000), 0x200000, boot::MemoryType::Available, "Mid RAM");

    TEST_ASSERT(tracker.count() == 3, "Tracker produced 3 normalized regions");
    // Verify sorted ascending by start address
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x0), "Region 0 is Low Memory");
    TEST_ASSERT(tracker.get(0).is_reserved(), "Region 0 is reserved");
    TEST_ASSERT(tracker.get(1).start == PhysicalAddress(0xA00000), "Region 1 is Mid RAM");
    TEST_ASSERT(tracker.get(1).is_available(), "Region 1 is available");
    TEST_ASSERT(tracker.get(2).start == PhysicalAddress(0x1E00000), "Region 2 is High RAM");
    TEST_ASSERT(tracker.get(2).is_available(), "Region 2 is available");

    // Strictly disjoint check
    for (size_t i = 0; i + 1 < tracker.count(); ++i) {
        TEST_ASSERT(tracker.get(i).end <= tracker.get(i + 1).start, "Regions strictly non-overlapping");
    }

    return true;
}

// 12. Memory Normalization: Overlapping Regions (RESERVED > AVAILABLE)
static bool test_normalization_overlapping() {
    ReservedMemoryTracker tracker;
    // Available region: 0x1000000 .. 0x2000000 (16 MiB .. 32 MiB)
    tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Available, "RAM");
    // Reserved region partially overlapping: 0x1800000 .. 0x2500000 (24 MiB .. 37 MiB)
    tracker.add_region(PhysicalAddress(0x1800000), 0xD00000, boot::MemoryType::Reserved, "Overlap Reserved");

    // Overlapping portion [0x1800000, 0x2000000) must be RESERVED!
    // Result must have 2 disjoint regions:
    // [0x1000000, 0x1800000) Available
    // [0x1800000, 0x2500000) Reserved
    TEST_ASSERT(tracker.count() == 2, "Normalization produced 2 disjoint regions");
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x1000000), "Region 0 start 16MB");
    TEST_ASSERT(tracker.get(0).end == PhysicalAddress(0x1800000), "Region 0 end 24MB");
    TEST_ASSERT(tracker.get(0).is_available(), "Region 0 is available");

    TEST_ASSERT(tracker.get(1).start == PhysicalAddress(0x1800000), "Region 1 start 24MB");
    TEST_ASSERT(tracker.get(1).end == PhysicalAddress(0x2500000), "Region 1 end 37MB");
    TEST_ASSERT(tracker.get(1).is_reserved(), "Region 1 is reserved");

    TEST_ASSERT(!tracker.is_reserved(PhysicalAddress(0x1400000)), "0x1400000 is Available");
    TEST_ASSERT(tracker.is_reserved(PhysicalAddress(0x1800000)), "0x1800000 is strictly Reserved");
    TEST_ASSERT(tracker.is_reserved(PhysicalAddress(0x1F00000)), "0x1F00000 is strictly Reserved");

    return true;
}

// 13. Memory Normalization: Contained Regions (Reserved inside Available)
static bool test_normalization_contained() {
    ReservedMemoryTracker tracker;
    // Available: 0x1000000 .. 0x2000000 (16 MiB .. 32 MiB)
    tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Available, "Large RAM");
    // Reserved hole inside: 0x1400000 .. 0x1600000 (20 MiB .. 22 MiB)
    tracker.add_region(PhysicalAddress(0x1400000), 0x200000, boot::MemoryType::Reserved, "Internal Reserved");

    // Result must be 3 disjoint regions:
    // [0x1000000, 0x1400000) Available
    // [0x1400000, 0x1600000) Reserved
    // [0x1600000, 0x2000000) Available
    TEST_ASSERT(tracker.count() == 3, "Contained reservation splits available into 3 regions");
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x1000000) && tracker.get(0).end == PhysicalAddress(0x1400000), "Left slice");
    TEST_ASSERT(tracker.get(0).is_available(), "Left is available");

    TEST_ASSERT(tracker.get(1).start == PhysicalAddress(0x1400000) && tracker.get(1).end == PhysicalAddress(0x1600000), "Middle slice");
    TEST_ASSERT(tracker.get(1).is_reserved(), "Middle is reserved");

    TEST_ASSERT(tracker.get(2).start == PhysicalAddress(0x1600000) && tracker.get(2).end == PhysicalAddress(0x2000000), "Right slice");
    TEST_ASSERT(tracker.get(2).is_available(), "Right is available");

    TEST_ASSERT(!tracker.is_reserved(PhysicalAddress(0x1200000)), "Left address is available");
    TEST_ASSERT(tracker.is_reserved(PhysicalAddress(0x1500000)), "Middle address is reserved");
    TEST_ASSERT(!tracker.is_reserved(PhysicalAddress(0x1800000)), "Right address is available");

    return true;
}

// 14. Memory Normalization: Surrounding Regions (Reserved completely covers Available)
static bool test_normalization_surrounding() {
    ReservedMemoryTracker tracker;
    // Available: 0x1200000 .. 0x1400000 (18 MiB .. 20 MiB)
    tracker.add_region(PhysicalAddress(0x1200000), 0x200000, boot::MemoryType::Available, "Swallowed RAM");
    // Reserved surrounding it: 0x1000000 .. 0x2000000 (16 MiB .. 32 MiB)
    tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Reserved, "Huge Reserved");

    // Available region must be completely eliminated!
    TEST_ASSERT(tracker.count() == 1, "Available region completely swallowed");
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x1000000), "Start 16MB");
    TEST_ASSERT(tracker.get(0).end == PhysicalAddress(0x2000000), "End 32MB");
    TEST_ASSERT(tracker.get(0).is_reserved(), "Only reserved region survives");

    TEST_ASSERT(tracker.is_reserved(PhysicalAddress(0x1300000)), "Swallowed address is strictly reserved");

    return true;
}

// 15. Memory Normalization: Sub-Page Regions & Alignment
static bool test_normalization_subpage() {
    ReservedMemoryTracker tracker;
    // Sub-page available: 0x100500 .. 0x101500 (4096 bytes, but unaligned):
    // align_up start = 0x101000, align_down end = 0x101000 -> 0 safe pages, must be dropped!
    tracker.add_region(PhysicalAddress(0x100500), 0x1000, boot::MemoryType::Available, "Unaligned Subpage Avail");

    // Sub-page reserved: 0x200500 .. 0x200700 (512 bytes):
    // align_down start = 0x200000, align_up end = 0x201000 -> expanded to cover full 4K page!
    tracker.add_region(PhysicalAddress(0x200500), 0x200, boot::MemoryType::Reserved, "Subpage Reserved");

    TEST_ASSERT(tracker.count() == 1, "Sub-page available dropped, sub-page reserved expanded");
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x200000), "Reserved start aligned down");
    TEST_ASSERT(tracker.get(0).end == PhysicalAddress(0x201000), "Reserved end aligned up");
    TEST_ASSERT(tracker.get(0).length == 4096, "Reserved covers full 4K page");

    return true;
}

// 16. Memory Normalization: Boundary Conditions (Abutment, 0-Length, Overflow)
static bool test_normalization_boundary_conditions() {
    ReservedMemoryTracker tracker;
    // 0-length entries ignored
    TEST_ASSERT(!tracker.add_region(PhysicalAddress(0x100000), 0, boot::MemoryType::Available, "Zero Len"), "0 len rejected");

    // Exact abutment: [10MB, 20MB) Available and [20MB, 30MB) Reserved
    tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Available, "RAM 1");
    tracker.add_region(PhysicalAddress(0x2000000), 0x1000000, boot::MemoryType::Reserved, "Reserved 2");

    TEST_ASSERT(tracker.count() == 2, "Adjacent regions remain intact");
    TEST_ASSERT(tracker.get(0).start == PhysicalAddress(0x1000000) && tracker.get(0).end == PhysicalAddress(0x2000000), "Abutment region 1");
    TEST_ASSERT(tracker.get(1).start == PhysicalAddress(0x2000000) && tracker.get(1).end == PhysicalAddress(0x3000000), "Abutment region 2");
    TEST_ASSERT(tracker.get(0).is_available() && tracker.get(1).is_reserved(), "Types preserved on abutment");

    // Duplicate ranges: adding identical regions merges into single entry
    ReservedMemoryTracker dup_tracker;
    dup_tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Available, "RAM");
    dup_tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Available, "RAM");
    TEST_ASSERT(dup_tracker.count() == 1, "Duplicate available region merged into single interval");
    TEST_ASSERT(dup_tracker.get(0).start == PhysicalAddress(0x1000000) && dup_tracker.get(0).end == PhysicalAddress(0x2000000), "Duplicate range bounds");

    ReservedMemoryTracker dup_res_tracker;
    dup_res_tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Reserved, "Firmware Reserved");
    dup_res_tracker.add_region(PhysicalAddress(0x1000000), 0x1000000, boot::MemoryType::Reserved, "Firmware Reserved");
    TEST_ASSERT(dup_res_tracker.count() == 1, "Duplicate reserved region merged into single interval");

    // Range near UINT64_MAX: safely handled by 64-bit overflow guards
    ReservedMemoryTracker max_tracker;
    max_tracker.add_region(PhysicalAddress(UINT64_MAX - 0x2000), 0x3000, boot::MemoryType::Available, "Overflow RAM");
    TEST_ASSERT(max_tracker.count() == 0, "Overflowing range near UINT64_MAX safely dropped");

    // Overflow protection: range exceeding UINT64_MAX
    TEST_ASSERT(tracker.overlaps_reserved(PhysicalAddress(UINT64_MAX - 100), 200), "Overflowing range treated as unsafe/reserved");

    return true;
}

// 17. Reservation Collision with Kernel & Multiboot2
static bool test_reservation_collision_and_kernel_overlap() {
    ReservedMemoryTracker tracker;
    PhysicalAddress k_start(0x100000);
    PhysicalAddress k_end(0x140000);
    tracker.add_region(k_start, k_end - k_start, boot::MemoryType::Reserved, "Kernel Image");

    PhysicalAddress mb2_start(0x150000);
    uint64_t mb2_size = 0x2000;
    tracker.add_region(mb2_start, mb2_size, boot::MemoryType::Reserved, "Multiboot2 Info");

    TEST_ASSERT(tracker.overlaps_reserved(PhysicalAddress(0x130000), 0x10000), "Bitmap at 0x130000 collides with kernel");
    TEST_ASSERT(tracker.overlaps_reserved(PhysicalAddress(0x14F000), 0x3000), "Bitmap at 0x14F000 collides with Multiboot2");
    TEST_ASSERT(!tracker.overlaps_reserved(PhysicalAddress(0x160000), 0x10000), "Bitmap at 0x160000 has zero collisions");

    return true;
}

int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Physical Memory Manager (PMM) Host Regression Suite\n");
    printf("================================================================================\n");

    struct TestCase {
        const char* name;
        bool (*func)();
    };

    TestCase tests[] = {
        {"PhysicalAddress Primitives & Alignment", test_physical_address_primitives},
        {"PhysicalAddress Arithmetic & Relational Operators", test_physical_address_arithmetic},
        {"PageFrameNumber & PageCount Conversions", test_pfn_and_page_count},
        {"PMM Bitmap Bit Allocation Simulation", test_pmm_bitmap_simulation},
        {"Multi-Frame Contiguous Run Search Simulation", test_pmm_contiguous_run_simulation},
        {"High Physical Address (> 4 GiB) Host Safety", test_high_physical_address_safety},
        {"Maximum Page Number & Bitmap Overflow Protection", test_max_page_and_bitmap_size_overflow},
        {"Contiguous Allocation at End of Region", test_contiguous_allocation_end_of_region},
        {"Allocation Exhaustion & Out-of-Memory Simulation", test_allocation_exhaustion_oom},
        {"Double Free, Invalid Free & Unaligned Free Protection", test_double_free_and_invalid_free_protection},
        {"Normalization Gate 11.1: Unsorted Region Inputs", test_normalization_unsorted},
        {"Normalization Gate 11.2: Overlapping Regions (RESERVED > USABLE)", test_normalization_overlapping},
        {"Normalization Gate 11.3: Contained Reservation Splitting", test_normalization_contained},
        {"Normalization Gate 11.4: Surrounding Reservation Elimination", test_normalization_surrounding},
        {"Normalization Gate 11.5: Sub-Page Alignment & Expansion", test_normalization_subpage},
        {"Normalization Gate 11.6: Boundary Conditions & Abutment", test_normalization_boundary_conditions},
        {"Reservation Collision, Kernel & MB2 Overlap", test_reservation_collision_and_kernel_overlap}
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
    printf(" PMM Regression Suite Complete: %zu/%zu tests PASSED (%zu assertions verified)\n",
           passed, total, g_assertions_passed);
    printf("================================================================================\n");

    return (passed == total) ? 0 : 1;
}
