#pragma once

#include "core/types.hpp"
#include "boot/boot_info.hpp"

// =============================================================================
// LlamaOS/A - Hardened Multiboot2 Specification Definitions and Parser
// =============================================================================
// Defines strongly-typed structures and strict validation interfaces for
// parsing bootloader-supplied Multiboot2 information blocks.
// Boot information abstraction is in "boot/boot_info.hpp".
// =============================================================================

namespace llamaos::boot {

constexpr uint32_t MULTIBOOT2_BOOTLOADER_MAGIC = 0x36d76289;
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

struct [[gnu::packed]] Multiboot2Tag {
    uint32_t type;
    uint32_t size;
};

struct [[gnu::packed]] Multiboot2InfoBlock {
    uint32_t total_size;
    uint32_t reserved;
};

using Multiboot2Info = BootInformation;

} // namespace llamaos::boot
