#include "multiboot2.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Hardened Multiboot2 Information Parser Implementation
// =============================================================================

namespace llamaos::boot {

BootInformation g_boot_info{};

namespace {

// Helper: Safely copy bounded string with guaranteed NUL termination
void safe_copy_string(char* dest, size_t dest_capacity, const char* src, size_t max_src_len) {
    if (!dest || dest_capacity == 0) return;
    dest[0] = '\0';
    if (!src || max_src_len == 0) return;

    size_t copy_len = 0;
    while (copy_len < max_src_len && src[copy_len] != '\0' && copy_len + 1 < dest_capacity) {
        dest[copy_len] = src[copy_len];
        copy_len++;
    }
    dest[copy_len] = '\0';
}

// Helper: Validate ACPI RSDP 1.0 / 2.0 checksum
bool validate_acpi_checksum(const uint8_t* rsdp, size_t length) {
    if (!rsdp || length == 0) return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += rsdp[i];
    }
    return (sum & 0xFF) == 0;
}

// Helper: Check if character is whitespace
inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

} // anonymous namespace

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

bool BootInformation::has_argument(const char* arg_name) const {
    if (!arg_name || arg_name[0] == '\0') return false;
    const size_t arg_len = strlen(arg_name);
    const char* p = command_line;

    while (*p != '\0') {
        // Skip leading whitespace
        while (*p != '\0' && is_space(*p)) {
            p++;
        }
        if (*p == '\0') break;

        const char* token_start = p;
        while (*p != '\0' && !is_space(*p)) {
            p++;
        }
        const size_t token_len = static_cast<size_t>(p - token_start);

        // Check for exact token match: e.g. "test"
        if (token_len == arg_len && strncmp(token_start, arg_name, arg_len) == 0) {
            return true;
        }

        // Check for key=value prefix match: e.g. "arg_name=..."
        if (token_len > arg_len && token_start[arg_len] == '=' &&
            strncmp(token_start, arg_name, arg_len) == 0) {
            return true;
        }
    }

    return false;
}

bool BootInformation::is_test_mode() const {
    return has_argument("test") || has_argument("selftest");
}

bool BootInformation::is_debug_mode() const {
    return has_argument("debug");
}

bool BootInformation::parse(uint64_t magic, uintptr_t info_addr) {
    memset(this, 0, sizeof(BootInformation));
    framebuffer.type = FramebufferType::Unknown;

    // 1. Magic check
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        valid = false;
        return false;
    }

    // 2. Base pointer null and alignment validation (must be 8-byte aligned)
    if (info_addr == 0 || (info_addr & 0x07) != 0) {
        valid = false;
        return false;
    }

    // 3. Header size and sanity limits
    const uint32_t total_size = *reinterpret_cast<const uint32_t*>(info_addr);
    if (total_size < 16 || total_size > MAX_REASONABLE_INFO_SIZE) {
        valid = false;
        return false;
    }

    // 4. Integer overflow check on address range
    const uintptr_t end = info_addr + total_size;
    if (end <= info_addr) {
        valid = false;
        return false;
    }

    uintptr_t curr = info_addr + 8; // Skip total_size and reserved fields
    bool found_terminator = false;

    while (curr < end) {
        // Ensure at least the 8-byte tag header is within bounds
        if (end - curr < 8) {
            break;
        }

        const uint32_t type = *reinterpret_cast<const uint32_t*>(curr);
        const uint32_t size = *reinterpret_cast<const uint32_t*>(curr + 4);

        // Terminal tag (TagType::End = 0)
        if (type == static_cast<uint32_t>(TagType::End)) {
            found_terminator = true;
            break;
        }

        // Tag size must be at least 8 bytes and must not exceed remaining buffer
        if (size < 8 || size > static_cast<uint32_t>(end - curr)) {
            break;
        }

        const uint32_t payload_len = size - 8;
        const uintptr_t payload_ptr = curr + 8;

        switch (static_cast<TagType>(type)) {
            case TagType::Cmdline: {
                if (payload_len > 0) {
                    safe_copy_string(command_line, sizeof(command_line),
                                     reinterpret_cast<const char*>(payload_ptr), payload_len);
                }
                break;
            }

            case TagType::BootLoaderName: {
                if (payload_len > 0) {
                    safe_copy_string(bootloader_name, sizeof(bootloader_name),
                                     reinterpret_cast<const char*>(payload_ptr), payload_len);
                }
                break;
            }

            case TagType::BasicMemInfo: {
                if (size >= 16) {
                    mem_lower_kb = *reinterpret_cast<const uint32_t*>(payload_ptr);
                    mem_upper_kb = *reinterpret_cast<const uint32_t*>(payload_ptr + 4);
                }
                break;
            }

            case TagType::Mmap: {
                if (size >= 16) {
                    const uint32_t entry_size = *reinterpret_cast<const uint32_t*>(payload_ptr);
                    // Standard Multiboot2 mmap entry is at least 24 bytes
                    if (entry_size >= sizeof(MemoryMapEntry) && entry_size <= 1024) {
                        const uintptr_t entries_start = curr + 16;
                        const uintptr_t entries_end = curr + size;

                        uintptr_t p = entries_start;
                        while (p < entries_end && (entries_end - p) >= sizeof(MemoryMapEntry)) {
                            const auto* src = reinterpret_cast<const MemoryMapEntry*>(p);

                            // Validate no 64-bit integer overflow in region range
                            if (src->base_addr + src->length >= src->base_addr) {
                                total_mmap_entries_detected++;

                                if (src->type == static_cast<uint32_t>(MemoryType::Available)) {
                                    if (UINT64_MAX - total_usable_ram_bytes >= src->length) {
                                        total_usable_ram_bytes += src->length;
                                    } else {
                                        total_usable_ram_bytes = UINT64_MAX;
                                        usable_ram_overflow = true;
                                    }
                                }

                                if (mmap_count < MAX_MEMORY_REGIONS) {
                                    mmap_entries[mmap_count++] = *src;
                                } else {
                                    mmap_truncated = true;
                                }
                            }

                            if (UINT64_MAX - p < entry_size) break;
                            p += entry_size;
                        }
                    }
                }
                break;
            }

            case TagType::Framebuffer: {
                if (size >= 32) {
                    const uint64_t fb_addr = *reinterpret_cast<const uint64_t*>(payload_ptr);
                    const uint32_t fb_pitch = *reinterpret_cast<const uint32_t*>(payload_ptr + 8);
                    const uint32_t fb_width = *reinterpret_cast<const uint32_t*>(payload_ptr + 12);
                    const uint32_t fb_height = *reinterpret_cast<const uint32_t*>(payload_ptr + 16);
                    const uint8_t  fb_bpp = *reinterpret_cast<const uint8_t*>(payload_ptr + 20);
                    const uint8_t  fb_type = *reinterpret_cast<const uint8_t*>(payload_ptr + 21);

                    // Dimension and sanity checks
                    if (fb_addr != 0 && fb_width > 0 && fb_width <= 7680 &&
                        fb_height > 0 && fb_height <= 4320 &&
                        (fb_bpp == 8 || fb_bpp == 15 || fb_bpp == 16 || fb_bpp == 24 || fb_bpp == 32) &&
                        fb_pitch >= ((static_cast<uint64_t>(fb_width) * fb_bpp + 7) / 8)) {

                        // Prevent 64-bit multiplication overflow on framebuffer span
                        const uint64_t total_fb_bytes = static_cast<uint64_t>(fb_pitch) * fb_height;
                        if (total_fb_bytes / fb_height == fb_pitch) {
                            framebuffer.address = fb_addr;
                            framebuffer.pitch   = fb_pitch;
                            framebuffer.width   = fb_width;
                            framebuffer.height  = fb_height;
                            framebuffer.bpp     = fb_bpp;

                            if (fb_type == 0) {
                                framebuffer.type = FramebufferType::Indexed;
                                framebuffer.valid = true;
                            } else if (fb_type == 1 && size >= 38) {
                                framebuffer.type = FramebufferType::DirectRgb;
                                framebuffer.red.position   = *reinterpret_cast<const uint8_t*>(payload_ptr + 24);
                                framebuffer.red.mask_size  = *reinterpret_cast<const uint8_t*>(payload_ptr + 25);
                                framebuffer.green.position = *reinterpret_cast<const uint8_t*>(payload_ptr + 26);
                                framebuffer.green.mask_size= *reinterpret_cast<const uint8_t*>(payload_ptr + 27);
                                framebuffer.blue.position  = *reinterpret_cast<const uint8_t*>(payload_ptr + 28);
                                framebuffer.blue.mask_size = *reinterpret_cast<const uint8_t*>(payload_ptr + 29);
                                framebuffer.valid = true;
                            } else if (fb_type == 2) {
                                framebuffer.type = FramebufferType::EgaText;
                                framebuffer.valid = true;
                            }
                        }
                    }
                }
                break;
            }

            case TagType::AcpiOld: {
                if (size >= 8 + 20) {
                    const auto* rsdp = reinterpret_cast<const uint8_t*>(payload_ptr);
                    // Check signature "RSD PTR "
                    if (memcmp(rsdp, "RSD PTR ", 8) == 0 && validate_acpi_checksum(rsdp, 20)) {
                        acpi.valid = true;
                        acpi.is_v2 = false;
                        acpi.revision = rsdp[15];
                        safe_copy_string(acpi.oem_id, sizeof(acpi.oem_id),
                                         reinterpret_cast<const char*>(rsdp + 9), 6);
                        acpi.rsdp_addr = payload_ptr;
                        acpi.length = 20;
                    }
                }
                break;
            }

            case TagType::AcpiNew: {
                if (size >= 8 + 36) {
                    const auto* rsdp = reinterpret_cast<const uint8_t*>(payload_ptr);
                    // Check signature and both 20-byte and 36-byte checksums
                    if (memcmp(rsdp, "RSD PTR ", 8) == 0 &&
                        validate_acpi_checksum(rsdp, 20) &&
                        validate_acpi_checksum(rsdp, 36)) {
                        acpi.valid = true;
                        acpi.is_v2 = true;
                        acpi.revision = rsdp[15];
                        safe_copy_string(acpi.oem_id, sizeof(acpi.oem_id),
                                         reinterpret_cast<const char*>(rsdp + 9), 6);
                        acpi.rsdp_addr = payload_ptr;
                        acpi.length = *reinterpret_cast<const uint32_t*>(rsdp + 20);
                        acpi.xsdt_address = *reinterpret_cast<const uint64_t*>(rsdp + 24);
                    }
                }
                break;
            }

            case TagType::Efi64: {
                if (size >= 16) {
                    const uint64_t sys_tab = *reinterpret_cast<const uint64_t*>(payload_ptr);
                    if (sys_tab != 0) {
                        efi.present = true;
                        efi.system_table_paddr = sys_tab;
                    }
                }
                break;
            }

            default:
                // Unknown tag: cleanly skip according to declared aligned size
                break;
        }

        // Align tag advance to 8-byte boundary with overflow check
        if (size > UINT32_MAX - 7) break;
        const uint32_t aligned_size = (size + 7) & ~7;
        if (curr + aligned_size <= curr) break;
        curr += aligned_size;
    }

    valid = found_terminator;
    return valid;
}

} // namespace llamaos::boot
