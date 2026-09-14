#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"

// =============================================================================
// LlamaOS/A - Task State Segment (TSS) and Interrupt Stack Table (IST)
// =============================================================================
// Defines the 64-bit Task State Segment and manages dedicated Interrupt Stack
// Tables (IST) for catastrophic CPU exceptions (Double Fault, Page Fault, etc.)
// Ensures every IST stack is PMM-allocated, page-aligned, mapped through the active
// VMM with RW NX permissions, and protected by non-present guard regions.
// =============================================================================

namespace llamaos::arch::x86_64 {

// Standard 64-bit Task State Segment layout (104 bytes)
struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0{0};
    uint64_t rsp0{0};
    uint64_t rsp1{0};
    uint64_t rsp2{0};
    uint64_t reserved1{0};
    uint64_t ist1{0}; // IST1: Dedicated Double Fault (#DF) stack
    uint64_t ist2{0}; // IST2: Dedicated Page Fault (#PF) stack
    uint64_t ist3{0}; // IST3: Dedicated NMI / MCE / Critical stack
    uint64_t ist4{0};
    uint64_t ist5{0};
    uint64_t ist6{0};
    uint64_t ist7{0};
    uint64_t reserved2{0};
    uint16_t reserved3{0};
    uint16_t iomap_base{sizeof(TaskStateSegment)}; // Points beyond segment (no I/O map)
};

static_assert(sizeof(TaskStateSegment) == 104, "TaskStateSegment must be exactly 104 bytes");

// Architectural IST Constants
inline constexpr size_t   IST_STACK_PAGES   = 4;                          // 16 KiB per stack
inline constexpr uint64_t IST_STACK_SIZE    = IST_STACK_PAGES * 4096ULL;  // 16384 bytes
inline constexpr size_t   GUARD_PAGE_PAGES  = 1;                          // 4 KiB guard page
inline constexpr uint64_t GUARD_PAGE_SIZE   = 4096ULL;

// Higher-half virtual address space window for IST stacks
// 0xFFFFFFFF70000000: Guard Page 1 (Unmapped)
// 0xFFFFFFFF70001000 - 0xFFFFFFFF70005000: IST1 (#DF Stack, 16 KiB)
// 0xFFFFFFFF70005000: Guard Page 2 (Unmapped)
// 0xFFFFFFFF70006000 - 0xFFFFFFFF7000A000: IST2 (#PF Stack, 16 KiB)
// 0xFFFFFFFF7000A000: Guard Page 3 (Unmapped)
// 0xFFFFFFFF7000B000 - 0xFFFFFFFF7000F000: IST3 (#NMI / Critical Stack, 16 KiB)
// 0xFFFFFFFF7000F000: Guard Page 4 (Unmapped)
// 0xFFFFFFFF70010000: TSS Page (4 KiB, mapped RW NX)
inline constexpr memory::VirtualAddress IST_VIRTUAL_WINDOW_BASE{0xFFFFFFFF70000000ULL};

inline constexpr memory::VirtualAddress IST1_GUARD_VIRTUAL{0xFFFFFFFF70000000ULL};
inline constexpr memory::VirtualAddress IST1_STACK_BOTTOM {0xFFFFFFFF70001000ULL};
inline constexpr memory::VirtualAddress IST1_STACK_TOP    {0xFFFFFFFF70005000ULL};

inline constexpr memory::VirtualAddress IST2_GUARD_VIRTUAL{0xFFFFFFFF70005000ULL};
inline constexpr memory::VirtualAddress IST2_STACK_BOTTOM {0xFFFFFFFF70006000ULL};
inline constexpr memory::VirtualAddress IST2_STACK_TOP    {0xFFFFFFFF7000A000ULL};

inline constexpr memory::VirtualAddress IST3_GUARD_VIRTUAL{0xFFFFFFFF7000A000ULL};
inline constexpr memory::VirtualAddress IST3_STACK_BOTTOM {0xFFFFFFFF7000B000ULL};
inline constexpr memory::VirtualAddress IST3_STACK_TOP    {0xFFFFFFFF7000F000ULL};

inline constexpr memory::VirtualAddress IST4_GUARD_VIRTUAL{0xFFFFFFFF7000F000ULL};
inline constexpr memory::VirtualAddress TSS_VIRTUAL_ADDR   {0xFFFFFFFF70010000ULL};

// TSS & IST Subsystem Manager
class TssManager {
public:
    // Allocates PMM frames, establishes VMM mappings with RW NX permissions, and initializes TSS
    static bool init(uint64_t kernel_rsp0);

    // Numerically and architecturally verifies TSS and IST mappings
    static bool verify();

    static TaskStateSegment* tss() noexcept { return s_tss; }
    static void set_rsp0(uint64_t rsp0) noexcept {
        if (s_tss) {
            s_tss->rsp0 = rsp0;
        }
    }
    static uint64_t tss_virtual_address() noexcept { return TSS_VIRTUAL_ADDR.value(); }
    static uint32_t tss_limit() noexcept { return sizeof(TaskStateSegment) - 1; }

    static memory::PhysicalAddress tss_physical_address() noexcept { return s_tss_paddr; }
    static memory::PhysicalAddress ist1_physical_address() noexcept { return s_ist1_paddr; }
    static memory::PhysicalAddress ist2_physical_address() noexcept { return s_ist2_paddr; }
    static memory::PhysicalAddress ist3_physical_address() noexcept { return s_ist3_paddr; }

private:
    static TaskStateSegment* s_tss;
    static memory::PhysicalAddress s_tss_paddr;
    static memory::PhysicalAddress s_ist1_paddr;
    static memory::PhysicalAddress s_ist2_paddr;
    static memory::PhysicalAddress s_ist3_paddr;
};

} // namespace llamaos::arch::x86_64
