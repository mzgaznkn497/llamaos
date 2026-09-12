#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Multiboot2 Specification Definitions and Parser
// =============================================================================

namespace llamaos::boot {

constexpr uint32_t MULTIBOOT2_BOOTLOADER_MAGIC = 0x36d76289;

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

struct FramebufferInfo {
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type;
    bool     available;
};

constexpr size_t MAX_MEMORY_REGIONS = 64;

struct BootInformation {
    bool valid;
    const char* command_line;
    const char* bootloader_name;
    uint32_t mem_lower_kb;
    uint32_t mem_upper_kb;
    uint64_t total_usable_ram_bytes;

    size_t mmap_count;
    MemoryMapEntry mmap_entries[MAX_MEMORY_REGIONS];

    FramebufferInfo framebuffer;
    uint64_t acpi_rsdp;
    uint64_t efi_system_table;

    void parse(uint64_t magic, uintptr_t info_addr);
    const char* memory_type_to_string(MemoryType type) const;
};

extern BootInformation g_boot_info;

} // namespace llamaos::boot
