#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Authoritative Boot Information Abstraction (Blocker 7)
// =============================================================================
// Isolates bootloader-specific structures (Multiboot2/GRUB) from kernel subsystems.
// Classified Subsystem Dependencies on Boot Information:
// 1. Memory Map: Physical Memory Manager (PMM) and ReservedMemoryTracker
// 2. Framebuffer: Linear Framebuffer Driver and Graphics Console
// 3. ACPI: Power Lifecycle Management (Shutdown, Reboot, Sleep states)
// 4. Command Line: Kernel boot options, root filesystem overrides, test flags
// 5. Firmware/EFI: UEFI System Table discovery and runtime service handoff
// =============================================================================

namespace llamaos::boot {

constexpr size_t MAX_CMDLINE_LEN = 256;
constexpr size_t MAX_LOADER_NAME_LEN = 64;
constexpr size_t MAX_MEMORY_REGIONS = 64;

enum class MemoryType : uint32_t {
    Available        = 1,
    Reserved         = 2,
    AcpiReclaimable  = 3,
    Nvs              = 4,
    BadRam           = 5
};

struct MemoryMapEntry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};
static_assert(sizeof(MemoryMapEntry) == 24, "MemoryMapEntry must be exactly 24 bytes");

enum class FramebufferType : uint8_t {
    Indexed = 0,
    DirectRgb = 1,
    EgaText = 2,
    Unknown = 255
};

struct FramebufferColorField {
    uint8_t position;
    uint8_t mask_size;
};

struct FramebufferInfo {
    bool valid;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    FramebufferType type;
    FramebufferColorField red;
    FramebufferColorField green;
    FramebufferColorField blue;
    uint64_t total_size_bytes;
};

struct AcpiInfo {
    bool valid;
    bool is_v2;
    uint8_t revision;
    char oem_id[7]; // 6 characters + NUL terminator
    uintptr_t mb2_rsdp_copy_addr;     // Pointer to payload copy of RSDP
    uint32_t length;                 // Structure length
    uint32_t rsdt_physical_address;  // Physical address of firmware RSDT
    uint64_t xsdt_physical_address;  // 64-bit physical address of firmware XSDT
};

struct EfiInfo {
    bool present;
    bool valid;
    bool is_physical;
    bool is_firmware_owned;
    uint64_t system_table_paddr;
};

enum class BootMode {
    Normal,
    Test,
    Debug
};

struct BootInformation {
    bool valid;
    bool mmap_truncated;
    bool usable_ram_overflow;

    char command_line[MAX_CMDLINE_LEN];
    char bootloader_name[MAX_LOADER_NAME_LEN];

    uint32_t mem_lower_kb;
    uint32_t mem_upper_kb;
    uint64_t total_usable_ram_bytes;

    size_t mmap_count;
    size_t total_mmap_entries_detected;
    MemoryMapEntry mmap_entries[MAX_MEMORY_REGIONS];

    FramebufferInfo framebuffer;
    AcpiInfo acpi;
    EfiInfo efi;

    // Bootloader information block memory reservation boundaries
    uint64_t mb2_info_paddr;
    uint32_t mb2_info_total_size;

    bool parse(uint64_t magic, uintptr_t info_addr);
    bool has_argument(const char* arg_name) const;
    bool get_argument_value(const char* key, char* dest, size_t dest_capacity) const;
    bool is_test_mode() const;
    bool is_debug_mode() const;
    BootMode get_boot_mode() const;
    const char* memory_type_to_string(MemoryType type) const;

    bool is_in_kernel_image(uint64_t paddr, uint64_t length) const;
    bool is_in_multiboot_info(uint64_t paddr, uint64_t length) const;
};

using BootInfo = BootInformation;

extern BootInformation g_boot_info;

} // namespace llamaos::boot
