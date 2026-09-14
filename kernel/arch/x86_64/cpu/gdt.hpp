#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Permanent Global Descriptor Table (GDT) and Selectors
// =============================================================================
// Establishes the permanent 64-bit Long Mode descriptor table, replacing the
// temporary bootstrap GDT. Configures kernel code, kernel data, and 64-bit
// Task State Segment (TSS) system descriptors with documented selector constants.
// =============================================================================

namespace llamaos::arch::x86_64 {

enum Selector : uint16_t {
    Null       = 0x00,
    KernelCode = 0x08,
    KernelData = 0x10,
    Tss        = 0x18,
    UserData   = 0x28, // Slot 5: User 64-bit Data (DPL 3 -> 0x2B)
    UserCode   = 0x30  // Slot 6: User 64-bit Code (DPL 3 -> 0x33)
};

// Standard 8-byte segment descriptor
struct [[gnu::packed]] GdtEntry {
    uint64_t raw{0};
};

// 16-byte 64-bit TSS descriptor (system descriptor occupying two GDT slots)
struct [[gnu::packed]] GdtTssEntry {
    uint64_t low{0};
    uint64_t high{0};
};

// Table containing 7 GDT slots (56 bytes total):
// Slot 0 (0x00): Null
// Slot 1 (0x08): Kernel 64-bit Code (DPL 0)
// Slot 2 (0x10): Kernel 64-bit Data (DPL 0)
// Slot 3 (0x18): TSS Low (System)
// Slot 4 (0x20): TSS High (System)
// Slot 5 (0x28): User 64-bit Data (DPL 3, RPL 3 = 0x2B)
// Slot 6 (0x30): User 64-bit Code (DPL 3, RPL 3 = 0x33)
struct [[gnu::packed]] GdtTable {
    GdtEntry entries[3];
    GdtTssEntry tss;
    GdtEntry user_data;
    GdtEntry user_code;
};

static_assert(sizeof(GdtTable) == 56, "GdtTable must be exactly 56 bytes");

// Descriptor encoding helper functions (accessible to host regression tests)
constexpr GdtEntry encode_code_descriptor(uint8_t dpl = 0) {
    // Limit: 0xFFFFF, Base: 0
    // Access: Present(1) | DPL(dpl) | System(1) | Executable(1) | Conforming(0) | Readable(1) | Accessed(0)
    // Flags: Granularity(1) | 64-bit Long Mode(1) | Size(0) | Available(0)
    uint64_t access = 0x9AULL | ((static_cast<uint64_t>(dpl) & 0x03) << 5);
    uint64_t flags = 0xAULL; // L=1, G=1, D=0
    uint64_t limit_low = 0xFFFFULL;
    uint64_t limit_high = 0x0FULL;

    uint64_t raw = limit_low
                 | (access << 40)
                 | (limit_high << 48)
                 | (flags << 52);
    return GdtEntry{raw};
}

constexpr GdtEntry encode_data_descriptor(uint8_t dpl = 0) {
    // Limit: 0xFFFFF, Base: 0
    // Access: Present(1) | DPL(dpl) | System(1) | Executable(0) | Direction(0) | Writable(1) | Accessed(0)
    // Flags: Granularity(1) | 64-bit Long Mode(0) | Size/Big(1) | Available(0)
    uint64_t access = 0x92ULL | ((static_cast<uint64_t>(dpl) & 0x03) << 5);
    uint64_t flags = 0xCULL; // G=1, B=1
    uint64_t limit_low = 0xFFFFULL;
    uint64_t limit_high = 0x0FULL;

    uint64_t raw = limit_low
                 | (access << 40)
                 | (limit_high << 48)
                 | (flags << 52);
    return GdtEntry{raw};
}

constexpr GdtTssEntry encode_tss_descriptor(uint64_t base, uint32_t limit, uint8_t dpl = 0) {
    GdtTssEntry entry{};
    uint64_t limit_low = limit & 0xFFFFULL;
    uint64_t base_low = (base & 0xFFFFULL) << 16;
    uint64_t base_mid = ((base >> 16) & 0xFFULL) << 32;
    // Type: 0x9 (64-bit TSS Available), S=0, DPL=dpl, P=1
    uint64_t access = (0x89ULL | ((static_cast<uint64_t>(dpl) & 0x03) << 5)) << 40;
    uint64_t limit_high = ((static_cast<uint64_t>(limit) >> 16) & 0x0FULL) << 48;
    uint64_t flags = 0ULL; // G=0 (byte granularity)
    uint64_t base_high_byte = ((base >> 24) & 0xFFULL) << 56;

    entry.low = limit_low | base_low | base_mid | access | limit_high | flags | base_high_byte;
    entry.high = (base >> 32) & 0xFFFFFFFFULL;
    return entry;
}

// Global Permanent GDT Manager
class PermanentGdt {
public:
    static void init(uint64_t tss_base, uint32_t tss_limit);
    static bool verify();
    static void install_user_descriptors();
    static bool has_user_descriptors() noexcept { return s_user_descriptors_installed; }

    static const GdtTable& table() noexcept { return s_table; }

private:
    static GdtTable s_table;
    static bool s_user_descriptors_installed;
};

// Architecture helpers
extern "C" {
    void reload_segments(uint16_t code_selector, uint16_t data_selector);
    void load_task_register(uint16_t tss_selector);
    uint16_t read_task_register();
    uint16_t read_cs();
    uint16_t read_ds();
    uint16_t read_ss();
}

} // namespace llamaos::arch::x86_64
