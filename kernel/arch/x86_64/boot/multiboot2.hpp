#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Hardened Multiboot2 Specification Definitions and Parser
// =============================================================================
// Defines strongly-typed structures and strict validation interfaces for
// parsing bootloader-supplied Multiboot2 information blocks.
// =============================================================================

namespace llamaos::boot {

constexpr uint32_t MULTIBOOT2_BOOTLOADER_MAGIC = 0x36d76289;
constexpr size_t MAX_CMDLINE_LEN = 256;
constexpr size_t MAX_LOADER_NAME_LEN = 64;
constexpr size_t MAX_MEMORY_REGIONS = 64;
constexpr uint32_t MAX_REASONABLE_INFO_SIZE = 8 * 1024 * 1024; // 8 MiB sanity ceiling

// Multiboot2 Tag Types
enum class TagType : uint32_t {
    End              = 0,
    Cmdline          = 1,
    BootLoaderName   = 2,
    BasicMemInfo     = 4,
    BiosBootDevice   = 5,
    Mmap             = 6,
    Vbe              = 7,
    Framebuffer      = 8,
    ElfSections      = 9,
    Apm              = 10,
    Efi32            = 11,
    Efi64            = 12,
    Smbios           = 13,
    AcpiOld          = 14,
    AcpiNew          = 15,
    Network          = 16,
    EfiMmap          = 17,
    EfiBs            = 18,
    Efi32Ih          = 19,
    Efi64Ih          = 20,
    LoadBaseAddr     = 21
};

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
};

struct AcpiInfo {
    bool valid;
    bool is_v2;
    uint8_t revision;
    char oem_id[7]; // 6 characters + NUL terminator
    uintptr_t rsdp_addr;
    uint32_t length;
    uint64_t xsdt_address;
};

struct EfiInfo {
    bool present;
    uint64_t system_table_paddr;
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

    bool parse(uint64_t magic, uintptr_t info_addr);
    bool has_argument(const char* arg_name) const;
    bool is_test_mode() const;
    bool is_debug_mode() const;
    const char* memory_type_to_string(MemoryType type) const;
};

extern BootInformation g_boot_info;

} // namespace llamaos::boot
