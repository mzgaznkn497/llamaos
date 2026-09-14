#include "multiboot2.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - Hardened Multiboot2 Information Parser Implementation
// =============================================================================

extern "C" {
    extern uint8_t _kernel_physical_start[] __attribute__((weak));
    extern uint8_t _kernel_physical_end[] __attribute__((weak));
}

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

bool BootInformation::get_argument_value(const char* key, char* dest, size_t dest_capacity) const {
    if (!key || key[0] == '\0' || !dest || dest_capacity == 0) return false;
    dest[0] = '\0';
    const size_t key_len = strlen(key);
    const char* p = command_line;

    while (*p != '\0') {
        while (*p != '\0' && is_space(*p)) p++;
        if (*p == '\0') break;

        const char* token_start = p;
        while (*p != '\0' && !is_space(*p)) p++;
        const size_t token_len = static_cast<size_t>(p - token_start);

        if (token_len > key_len && token_start[key_len] == '=' &&
            strncmp(token_start, key, key_len) == 0) {
            const char* val_start = token_start + key_len + 1;
            const size_t val_len = token_len - key_len - 1;
            safe_copy_string(dest, dest_capacity, val_start, val_len);
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

BootMode BootInformation::get_boot_mode() const {
    if (is_test_mode()) {
        return BootMode::Test;
    }
    if (is_debug_mode()) {
        return BootMode::Debug;
    }
    return BootMode::Normal;
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
    if (UINTPTR_MAX - info_addr < total_size) {
        valid = false;
        return false;
    }
    const uintptr_t end = info_addr + total_size;

    // Save physical/virtual base of information block for reservation planning
    mb2_info_paddr = (info_addr >= KERNEL_VIRTUAL_BASE) ? (info_addr - KERNEL_VIRTUAL_BASE) : static_cast<uint64_t>(info_addr);
    mb2_info_total_size = total_size;

    // Duplicate tag suppression flags
    bool seen_cmdline = false;
    bool seen_bootloader_name = false;
    bool seen_basic_mem = false;
    bool seen_mmap = false;
    bool seen_framebuffer = false;
    bool seen_acpi_old = false;
    bool seen_acpi_new = false;
    bool seen_efi64 = false;

    uintptr_t curr = info_addr + 8; // Skip total_size and reserved fields
    bool found_terminator = false;

    while (curr < end) {
        // Ensure at least the 8-byte tag header is within bounds
        if (end - curr < 8) {
            break;
        }

        const uint32_t type = *reinterpret_cast<const uint32_t*>(curr);
        const uint32_t size = *reinterpret_cast<const uint32_t*>(curr + 4);

        // Terminal tag (TagType::End = 0): Multiboot2 specification dictates size must be 8
        if (type == static_cast<uint32_t>(TagType::End)) {
            if (size == 8 && (end - curr >= 8)) {
                found_terminator = true;
            }
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
                if (!seen_cmdline) {
                    seen_cmdline = true;
                    if (payload_len > 0) {
                        safe_copy_string(command_line, sizeof(command_line),
                                         reinterpret_cast<const char*>(payload_ptr), payload_len);
                    }
                }
                break;
            }

            case TagType::BootLoaderName: {
                if (!seen_bootloader_name) {
                    seen_bootloader_name = true;
                    if (payload_len > 0) {
                        safe_copy_string(bootloader_name, sizeof(bootloader_name),
                                         reinterpret_cast<const char*>(payload_ptr), payload_len);
                    }
                }
                break;
            }

            case TagType::BasicMemInfo: {
                if (!seen_basic_mem) {
                    seen_basic_mem = true;
                    if (size >= 16) {
                        mem_lower_kb = *reinterpret_cast<const uint32_t*>(payload_ptr);
                        mem_upper_kb = *reinterpret_cast<const uint32_t*>(payload_ptr + 4);
                    }
                }
                break;
            }

            case TagType::Mmap: {
                if (seen_mmap) {
                    // Duplicate memory map tag encountered: ignore to avoid double-counting
                    break;
                }
                seen_mmap = true;

                if (size >= 16) {
                    const uint32_t entry_size = *reinterpret_cast<const uint32_t*>(payload_ptr);
                    const uint32_t entry_version = *reinterpret_cast<const uint32_t*>(payload_ptr + 4);

                    // Standard Multiboot2 mmap entry: version 0, size at least 24 bytes
                    if (entry_version == 0 && entry_size >= sizeof(MemoryMapEntry) && entry_size <= 1024) {
                        const uint32_t payload_entries_bytes = size - 16;
                        if ((payload_entries_bytes % entry_size) != 0) {
                            mmap_truncated = true;
                        }

                        const uintptr_t entries_start = curr + 16;
                        const uintptr_t entries_end = curr + size;

                        uintptr_t p = entries_start;
                        while (p < entries_end && (entries_end - p) >= entry_size) {
                            const auto* src = reinterpret_cast<const MemoryMapEntry*>(p);

                            // Validate no 64-bit integer overflow and non-zero length
                            if (src->length > 0 && (UINT64_MAX - src->base_addr >= src->length)) {
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

                            if (UINTPTR_MAX - p < entry_size) break;
                            p += entry_size;
                        }
                    }
                }
                break;
            }

            case TagType::Framebuffer: {
                if (seen_framebuffer) {
                    break; // Duplicate framebuffer tag
                }
                seen_framebuffer = true;

                if (size >= 32) {
                    const uint64_t fb_addr   = *reinterpret_cast<const uint64_t*>(payload_ptr);
                    const uint32_t fb_pitch  = *reinterpret_cast<const uint32_t*>(payload_ptr + 8);
                    const uint32_t fb_width  = *reinterpret_cast<const uint32_t*>(payload_ptr + 12);
                    const uint32_t fb_height = *reinterpret_cast<const uint32_t*>(payload_ptr + 16);
                    const uint8_t  fb_bpp    = *reinterpret_cast<const uint8_t*>(payload_ptr + 20);
                    const uint8_t  fb_type   = *reinterpret_cast<const uint8_t*>(payload_ptr + 21);

                    // Dimension, address, and bpp sanity checks
                    if (fb_addr != 0 && fb_width > 0 && fb_width <= 7680 &&
                        fb_height > 0 && fb_height <= 4320 &&
                        (fb_bpp == 8 || fb_bpp == 15 || fb_bpp == 16 || fb_bpp == 24 || fb_bpp == 32)) {

                        const uint64_t min_pitch = (static_cast<uint64_t>(fb_width) * fb_bpp + 7) / 8;
                        const uint64_t max_pitch = static_cast<uint64_t>(fb_width) * 64ULL;
                        if (fb_pitch >= min_pitch && fb_pitch <= max_pitch && (fb_pitch <= UINT64_MAX / fb_height)) {
                            const uint64_t total_fb_bytes = static_cast<uint64_t>(fb_pitch) * fb_height;
                            if (UINT64_MAX - fb_addr >= total_fb_bytes) {
                                if (fb_type == 0 && size >= 36) { // Indexed
                                    const uint32_t num_colors = *reinterpret_cast<const uint32_t*>(payload_ptr + 24);
                                    if (num_colors <= 256 && (36 + num_colors * 3 <= size)) {
                                        framebuffer.address = fb_addr;
                                        framebuffer.pitch   = fb_pitch;
                                        framebuffer.width   = fb_width;
                                        framebuffer.height  = fb_height;
                                        framebuffer.bpp     = fb_bpp;
                                        framebuffer.type    = FramebufferType::Indexed;
                                        framebuffer.total_size_bytes = total_fb_bytes;
                                        framebuffer.valid   = true;
                                    }
                                } else if (fb_type == 1 && size >= 38) { // Direct RGB
                                    framebuffer.address = fb_addr;
                                    framebuffer.pitch   = fb_pitch;
                                    framebuffer.width   = fb_width;
                                    framebuffer.height  = fb_height;
                                    framebuffer.bpp     = fb_bpp;
                                    framebuffer.type    = FramebufferType::DirectRgb;
                                    framebuffer.red.position    = *reinterpret_cast<const uint8_t*>(payload_ptr + 24);
                                    framebuffer.red.mask_size   = *reinterpret_cast<const uint8_t*>(payload_ptr + 25);
                                    framebuffer.green.position  = *reinterpret_cast<const uint8_t*>(payload_ptr + 26);
                                    framebuffer.green.mask_size = *reinterpret_cast<const uint8_t*>(payload_ptr + 27);
                                    framebuffer.blue.position   = *reinterpret_cast<const uint8_t*>(payload_ptr + 28);
                                    framebuffer.blue.mask_size  = *reinterpret_cast<const uint8_t*>(payload_ptr + 29);
                                    framebuffer.total_size_bytes = total_fb_bytes;
                                    framebuffer.valid   = true;
                                } else if (fb_type == 2) { // EGA text
                                    framebuffer.address = fb_addr;
                                    framebuffer.pitch   = fb_pitch;
                                    framebuffer.width   = fb_width;
                                    framebuffer.height  = fb_height;
                                    framebuffer.bpp     = fb_bpp;
                                    framebuffer.type    = FramebufferType::EgaText;
                                    framebuffer.total_size_bytes = total_fb_bytes;
                                    framebuffer.valid   = true;
                                }
                            }
                        }
                    }
                }
                break;
            }

            case TagType::AcpiOld: {
                if (seen_acpi_new || seen_acpi_old) {
                    break; // Prefer ACPI 2.0 or ignore duplicate ACPI 1.0
                }
                seen_acpi_old = true;

                if (size >= 8 + 20) {
                    const auto* rsdp = reinterpret_cast<const uint8_t*>(payload_ptr);
                    if (memcmp(rsdp, "RSD PTR ", 8) == 0 && validate_acpi_checksum(rsdp, 20)) {
                        acpi.valid = true;
                        acpi.is_v2 = false;
                        acpi.revision = rsdp[15];
                        safe_copy_string(acpi.oem_id, sizeof(acpi.oem_id),
                                         reinterpret_cast<const char*>(rsdp + 9), 6);
                        acpi.mb2_rsdp_copy_addr = payload_ptr;
                        acpi.length = 20;
                        acpi.rsdt_physical_address = *reinterpret_cast<const uint32_t*>(rsdp + 16);
                        acpi.xsdt_physical_address = 0;
                    }
                }
                break;
            }

            case TagType::AcpiNew: {
                if (seen_acpi_new) {
                    break; // Duplicate ACPI 2.0
                }
                seen_acpi_new = true;

                if (size >= 8 + 36) {
                    const auto* rsdp = reinterpret_cast<const uint8_t*>(payload_ptr);
                    const uint32_t declared_len = *reinterpret_cast<const uint32_t*>(rsdp + 20);
                    if (declared_len >= 36 && declared_len <= payload_len) {
                        if (memcmp(rsdp, "RSD PTR ", 8) == 0 &&
                            validate_acpi_checksum(rsdp, 20) &&
                            validate_acpi_checksum(rsdp, declared_len)) {
                            acpi.valid = true;
                            acpi.is_v2 = true;
                            acpi.revision = rsdp[15];
                            safe_copy_string(acpi.oem_id, sizeof(acpi.oem_id),
                                             reinterpret_cast<const char*>(rsdp + 9), 6);
                            acpi.mb2_rsdp_copy_addr = payload_ptr;
                            acpi.length = declared_len;
                            acpi.rsdt_physical_address = *reinterpret_cast<const uint32_t*>(rsdp + 16);
                            acpi.xsdt_physical_address = *reinterpret_cast<const uint64_t*>(rsdp + 24);
                        }
                    }
                }
                break;
            }

            case TagType::Efi64: {
                if (seen_efi64) {
                    break; // Duplicate EFI64 tag
                }
                seen_efi64 = true;

                if (size >= 16) {
                    const uint64_t sys_tab = *reinterpret_cast<const uint64_t*>(payload_ptr);
                    if (sys_tab != 0) {
                        efi.present = true;
                        efi.valid = true;
                        efi.is_physical = true;
                        efi.is_firmware_owned = true;
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
        if (UINTPTR_MAX - curr < aligned_size) break;
        curr += aligned_size;
    }

    valid = found_terminator;
    return valid;
}

} // namespace llamaos::boot
