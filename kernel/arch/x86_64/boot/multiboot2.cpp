#include "multiboot2.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Multiboot2 Information Parser Implementation
// =============================================================================

namespace llamaos::boot {

BootInformation g_boot_info{};

const char* BootInformation::memory_type_to_string(MemoryType type) const {
    switch (type) {
        case MemoryType::Available:       return "Usable RAM";
        case MemoryType::Reserved:        return "Reserved";
        case MemoryType::AcpiReclaimable: return "ACPI Reclaimable";
        case MemoryType::Nvs:             return "ACPI NVS";
        case MemoryType::BadRam:          return "Bad Memory";
        default:                          return "Unknown";
    }
}

void BootInformation::parse(uint64_t magic, uintptr_t info_addr) {
    memset(this, 0, sizeof(BootInformation));

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || info_addr == 0) {
        valid = false;
        return;
    }

    valid = true;
    uint32_t total_size = *reinterpret_cast<const uint32_t*>(info_addr);
    uintptr_t curr = info_addr + 8;
    uintptr_t end = info_addr + total_size;

    while (curr < end) {
        uint32_t type = *reinterpret_cast<const uint32_t*>(curr);
        uint32_t size = *reinterpret_cast<const uint32_t*>(curr + 4);

        if (type == static_cast<uint32_t>(TagType::End)) {
            break;
        }

        if (size == 0) {
            break; // Guard against infinite loop if malformed
        }

        switch (static_cast<TagType>(type)) {
            case TagType::Cmdline: {
                command_line = reinterpret_cast<const char*>(curr + 8);
                break;
            }
            case TagType::BootLoaderName: {
                bootloader_name = reinterpret_cast<const char*>(curr + 8);
                break;
            }
            case TagType::BasicMemInfo: {
                mem_lower_kb = *reinterpret_cast<const uint32_t*>(curr + 8);
                mem_upper_kb = *reinterpret_cast<const uint32_t*>(curr + 12);
                break;
            }
            case TagType::Mmap: {
                uint32_t entry_size = *reinterpret_cast<const uint32_t*>(curr + 8);
                // uint32_t entry_version = *reinterpret_cast<const uint32_t*>(curr + 12);
                uintptr_t entries_start = curr + 16;
                uintptr_t entries_end = curr + size;

                mmap_count = 0;
                total_usable_ram_bytes = 0;

                for (uintptr_t p = entries_start; p < entries_end && mmap_count < MAX_MEMORY_REGIONS; p += entry_size) {
                    const auto* src_entry = reinterpret_cast<const MemoryMapEntry*>(p);
                    mmap_entries[mmap_count] = *src_entry;

                    if (src_entry->type == static_cast<uint32_t>(MemoryType::Available)) {
                        total_usable_ram_bytes += src_entry->length;
                    }
                    mmap_count++;
                }
                break;
            }
            case TagType::Framebuffer: {
                framebuffer.address = *reinterpret_cast<const uint64_t*>(curr + 8);
                framebuffer.pitch   = *reinterpret_cast<const uint32_t*>(curr + 16);
                framebuffer.width   = *reinterpret_cast<const uint32_t*>(curr + 20);
                framebuffer.height  = *reinterpret_cast<const uint32_t*>(curr + 24);
                framebuffer.bpp     = *reinterpret_cast<const uint8_t*>(curr + 28);
                framebuffer.type    = *reinterpret_cast<const uint8_t*>(curr + 29);
                framebuffer.available = true;
                break;
            }
            case TagType::AcpiOld: {
                acpi_rsdp = curr + 8;
                break;
            }
            case TagType::AcpiNew: {
                acpi_rsdp = curr + 8;
                break;
            }
            case TagType::Efi64: {
                efi_system_table = *reinterpret_cast<const uint64_t*>(curr + 8);
                break;
            }
            default:
                break;
        }

        // Multiboot2 tags are 8-byte aligned
        curr += (size + 7) & ~7;
    }
}

} // namespace llamaos::boot
