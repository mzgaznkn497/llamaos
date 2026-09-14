#include "arch/x86_64/boot/multiboot2.hpp"
#include "core/string.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

// =============================================================================
// LlamaOS/A - Multiboot2 Parser & Command-Line Comprehensive Regression Suite
// =============================================================================

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return false; \
        } \
    } while (0)

using namespace llamaos::boot;

// Helper to construct a minimal valid Multiboot2 tag buffer
static std::vector<uint8_t> create_base_mb2() {
    std::vector<uint8_t> buf;
    // total_size (uint32_t) + reserved (uint32_t) = 8 bytes
    // End tag: type 0 (uint32_t), size 8 (uint32_t) = 8 bytes
    buf.resize(16, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 16;
    *reinterpret_cast<uint32_t*>(&buf[4]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 0; // End tag type
    *reinterpret_cast<uint32_t*>(&buf[12]) = 8; // End tag size
    return buf;
}

// 1. Invalid Magic
static bool test_invalid_magic() {
    auto buf = create_base_mb2();
    BootInformation info;
    bool res = info.parse(0xDEADBEEF, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false for invalid magic");
    TEST_ASSERT(!info.valid, "Expected info.valid == false");
    return true;
}

// 2. Zero Info Address
static bool test_zero_info_address() {
    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, 0);
    TEST_ASSERT(!res, "Expected false for zero info address");
    TEST_ASSERT(!info.valid, "Expected info.valid == false");
    return true;
}

// 3. Unaligned Info Address
static bool test_unaligned_info_address() {
    auto buf = create_base_mb2();
    BootInformation info;
    uintptr_t unaligned = reinterpret_cast<uintptr_t>(buf.data()) | 3;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, unaligned);
    TEST_ASSERT(!res, "Expected false for unaligned address");
    return true;
}

// 4. Too-Small Total Size (< 16)
static bool test_too_small_total_size() {
    auto buf = create_base_mb2();
    *reinterpret_cast<uint32_t*>(&buf[0]) = 8; // Less than 16
    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false for total_size < 16");
    return true;
}

// 5. Overflowing Total Size
static bool test_overflowing_total_size() {
    auto buf = create_base_mb2();
    *reinterpret_cast<uint32_t*>(&buf[0]) = 0xFFFFFFF0U;
    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false for overflowing total_size");
    return true;
}

// 6. Zero Tag Size
static bool test_zero_tag_size() {
    std::vector<uint8_t> buf(32, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 32; // total_size
    *reinterpret_cast<uint32_t*>(&buf[8]) = 1;  // Tag 1 (Cmdline)
    *reinterpret_cast<uint32_t*>(&buf[12]) = 0; // Size = 0 (MALFORMED)

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false when tag size is 0");
    return true;
}

// 7. Tag Size Less Than Header (< 8)
static bool test_tag_size_less_than_header() {
    std::vector<uint8_t> buf(32, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 32;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 1;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 4; // Size = 4 (< 8)

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false when tag size < 8");
    return true;
}

// 8. Oversized Tag Exceeding Total Buffer
static bool test_oversized_tag() {
    std::vector<uint8_t> buf(32, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 32;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 1;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 64; // Declared size > buffer

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false when tag extends past buffer");
    return true;
}

// 9. Malformed Terminating Tag Size
static bool test_malformed_terminator_size() {
    std::vector<uint8_t> buf(32, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 32;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 0;  // TagType::End
    *reinterpret_cast<uint32_t*>(&buf[12]) = 16; // Malformed size (must be exactly 8)

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false when terminator tag size != 8");
    TEST_ASSERT(!info.valid, "Expected info.valid == false for malformed terminator");
    return true;
}

// 10. Truncated Tag Header (less than 8 bytes remaining for next tag)
static bool test_truncated_tag_header() {
    std::vector<uint8_t> buf(20, 0); // 8 header + 8 tag1 + 4 leftover
    *reinterpret_cast<uint32_t*>(&buf[0]) = 20;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 2; // BootLoaderName
    *reinterpret_cast<uint32_t*>(&buf[12]) = 8;
    // 4 trailing bytes remain (cannot hold 8-byte tag header or End tag)

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!res, "Expected false when buffer truncates before terminating tag");
    return true;
}

// 11. Unknown Tag Skipping
static bool test_unknown_tag_skipped() {
    std::vector<uint8_t> buf(48, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 48;

    // Tag 1: Unknown tag 0x9999 (16 bytes)
    *reinterpret_cast<uint32_t*>(&buf[8]) = 0x9999;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 16;

    // Tag 2: Cmdline tag (16 bytes)
    *reinterpret_cast<uint32_t*>(&buf[24]) = 1;
    *reinterpret_cast<uint32_t*>(&buf[28]) = 16;
    strcpy(reinterpret_cast<char*>(&buf[32]), "test");

    // Tag 3: End tag (8 bytes)
    *reinterpret_cast<uint32_t*>(&buf[40]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[44]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Unknown tag must be skipped and following tags parsed");
    TEST_ASSERT(info.valid, "Expected info.valid == true");
    TEST_ASSERT(info.is_test_mode(), "Expected test mode detected after unknown tag");
    return true;
}

// 12. Duplicate Tag Protection (Memory Map & Cmdline)
static bool test_duplicate_tags_protection() {
    std::vector<uint8_t> buf(128, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 128;

    // First Cmdline: "test"
    *reinterpret_cast<uint32_t*>(&buf[8]) = 1;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 16;
    strcpy(reinterpret_cast<char*>(&buf[16]), "test");

    // Second Cmdline (Duplicate): "debug"
    *reinterpret_cast<uint32_t*>(&buf[24]) = 1;
    *reinterpret_cast<uint32_t*>(&buf[28]) = 16;
    strcpy(reinterpret_cast<char*>(&buf[32]), "debug");

    // First Mmap: 1 region of 1000 bytes available
    *reinterpret_cast<uint32_t*>(&buf[40]) = 6;
    *reinterpret_cast<uint32_t*>(&buf[44]) = 40; // 16 header + 24 entry
    *reinterpret_cast<uint32_t*>(&buf[48]) = 24; // entry_size
    *reinterpret_cast<uint32_t*>(&buf[52]) = 0;  // entry_version
    *reinterpret_cast<uint64_t*>(&buf[56]) = 0x1000;
    *reinterpret_cast<uint64_t*>(&buf[64]) = 1000;
    *reinterpret_cast<uint32_t*>(&buf[72]) = 1; // Available

    // Second Mmap (Duplicate): 1 region of 1000 bytes available
    *reinterpret_cast<uint32_t*>(&buf[80]) = 6;
    *reinterpret_cast<uint32_t*>(&buf[84]) = 40;
    *reinterpret_cast<uint32_t*>(&buf[88]) = 24;
    *reinterpret_cast<uint32_t*>(&buf[92]) = 0;
    *reinterpret_cast<uint64_t*>(&buf[96]) = 0x2000;
    *reinterpret_cast<uint64_t*>(&buf[104]) = 1000;
    *reinterpret_cast<uint32_t*>(&buf[112]) = 1;

    // End tag
    *reinterpret_cast<uint32_t*>(&buf[120]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[124]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    // Duplicate cmdline must not overwrite first
    TEST_ASSERT(info.is_test_mode(), "First cmdline 'test' must be preserved");
    // Duplicate mmap must not double-count RAM
    TEST_ASSERT(info.total_usable_ram_bytes == 1000, "RAM must not be double counted by duplicate mmap tag");
    TEST_ASSERT(info.mmap_count == 1, "Duplicate mmap entries must be ignored");
    return true;
}

// 13. Unterminated Cmdline String
static bool test_unterminated_cmdline() {
    std::vector<uint8_t> buf;
    buf.resize(40, 'X'); // Fill with 'X' (no NUL)
    *reinterpret_cast<uint32_t*>(&buf[0]) = 40; // total_size
    *reinterpret_cast<uint32_t*>(&buf[4]) = 0;  // reserved
    *reinterpret_cast<uint32_t*>(&buf[8]) = 1;  // Cmdline tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 16; // 8 bytes header + 8 bytes payload (all 'X')
    *reinterpret_cast<uint32_t*>(&buf[24]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[28]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Expected true for valid structure with unterminated string");
    TEST_ASSERT(info.command_line[8] == '\0', "Expected string to be safely NUL-terminated in buffer");
    return true;
}

// 14. Malformed Memory Map Entry Size (< 24)
static bool test_malformed_mmap_entry_size() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 6;  // Mmap tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 40; // size
    *reinterpret_cast<uint32_t*>(&buf[16]) = 12; // Entry size = 12 (< 24, INVALID)
    *reinterpret_cast<uint32_t*>(&buf[20]) = 0;  // Entry version

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Buffer structure itself is valid");
    TEST_ASSERT(info.mmap_count == 0, "Malformed entry_size must result in 0 entries parsed");
    return true;
}

// 15. Malformed Memory Map Entry Version (!= 0)
static bool test_malformed_mmap_entry_version() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 6;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 40;
    *reinterpret_cast<uint32_t*>(&buf[16]) = 24;
    *reinterpret_cast<uint32_t*>(&buf[20]) = 1; // Entry version = 1 (INVALID, must be 0)

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Buffer structure itself is valid");
    TEST_ASSERT(info.mmap_count == 0, "Non-zero entry_version must result in 0 entries parsed");
    return true;
}

// 16. Trailing Partial Memory Map Entry
static bool test_mmap_trailing_partial_entry() {
    std::vector<uint8_t> buf(80, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 80;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 6;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 50; // 16 header + 24 entry + 10 dangling partial bytes
    *reinterpret_cast<uint32_t*>(&buf[16]) = 24;
    *reinterpret_cast<uint32_t*>(&buf[20]) = 0;

    // Entry 1
    *reinterpret_cast<uint64_t*>(&buf[24]) = 0x1000;
    *reinterpret_cast<uint64_t*>(&buf[32]) = 0x5000;
    *reinterpret_cast<uint32_t*>(&buf[40]) = 1;

    // Aligned advance to next tag: (50 + 7) & ~7 = 56
    *reinterpret_cast<uint32_t*>(&buf[64]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[68]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Buffer should be accepted");
    TEST_ASSERT(info.mmap_truncated, "Trailing partial bytes must set mmap_truncated flag");
    TEST_ASSERT(info.mmap_count == 1, "Only complete entry must be parsed");
    return true;
}

// 17. Memory Map with Future Larger Entry Size (e.g. 32 bytes)
static bool test_mmap_future_larger_entry() {
    std::vector<uint8_t> buf(96, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 96;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 6;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 80; // 16 header + 2 * 32 bytes
    *reinterpret_cast<uint32_t*>(&buf[16]) = 32; // Future entry size
    *reinterpret_cast<uint32_t*>(&buf[20]) = 0;

    // Entry 1 (offset 24)
    *reinterpret_cast<uint64_t*>(&buf[24]) = 0x100000;
    *reinterpret_cast<uint64_t*>(&buf[32]) = 0x200000;
    *reinterpret_cast<uint32_t*>(&buf[40]) = 1;

    // Entry 2 (offset 24 + 32 = 56)
    *reinterpret_cast<uint64_t*>(&buf[56]) = 0x300000;
    *reinterpret_cast<uint64_t*>(&buf[64]) = 0x100000;
    *reinterpret_cast<uint32_t*>(&buf[72]) = 1;

    // End tag (offset 8 + 80 = 88)
    *reinterpret_cast<uint32_t*>(&buf[88]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[92]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed with larger entry_size");
    TEST_ASSERT(info.mmap_count == 2, "Both 32-byte entries must be parsed");
    TEST_ASSERT(info.total_usable_ram_bytes == 0x300000, "Summed RAM must match 0x200000 + 0x100000");
    return true;
}

// 18. Memory Map Usable RAM Calculation & Saturation
static bool test_mmap_usable_ram_overflow() {
    std::vector<uint8_t> buf(96, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 96;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 6;  // Mmap tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 64; // size
    *reinterpret_cast<uint32_t*>(&buf[16]) = 24; // entry_size
    *reinterpret_cast<uint32_t*>(&buf[20]) = 0;  // entry_version

    // Entry 1: Base 0, Length 0xFFFFFFFFFFFFFFFE (Type 1: Available)
    *reinterpret_cast<uint64_t*>(&buf[24]) = 0x0;
    *reinterpret_cast<uint64_t*>(&buf[32]) = 0xFFFFFFFFFFFFFFFEULL;
    *reinterpret_cast<uint32_t*>(&buf[40]) = 1;

    // Entry 2: Base 0, Length 0x100 (Type 1: Available) -> Will cause total_usable_ram_bytes overflow!
    *reinterpret_cast<uint64_t*>(&buf[48]) = 0x0;
    *reinterpret_cast<uint64_t*>(&buf[56]) = 0x100;
    *reinterpret_cast<uint32_t*>(&buf[64]) = 1;

    *reinterpret_cast<uint32_t*>(&buf[72]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[76]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.usable_ram_overflow, "Expected usable_ram_overflow == true");
    TEST_ASSERT(info.total_usable_ram_bytes == UINT64_MAX, "Expected saturated total_usable_ram_bytes");
    return true;
}

// 19. Framebuffer Valid Metadata
static bool test_framebuffer_validation_valid() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 8;  // Framebuffer tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 40; // size
    *reinterpret_cast<uint64_t*>(&buf[16]) = 0xE0000000; // addr
    *reinterpret_cast<uint32_t*>(&buf[24]) = 4096; // pitch
    *reinterpret_cast<uint32_t*>(&buf[28]) = 1024; // width
    *reinterpret_cast<uint32_t*>(&buf[32]) = 768;  // height
    buf[36] = 32; // bpp
    buf[37] = 1;  // type DirectRgb
    // RGB positions
    buf[38] = 16; buf[39] = 8; buf[40] = 8; buf[41] = 8; buf[42] = 0; buf[43] = 8;

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.framebuffer.valid, "Expected framebuffer.valid == true");
    TEST_ASSERT(info.framebuffer.width == 1024, "Expected width == 1024");
    TEST_ASSERT(info.framebuffer.height == 768, "Expected height == 768");
    TEST_ASSERT(info.framebuffer.total_size_bytes == 4096ULL * 768ULL, "Expected correct total size");
    TEST_ASSERT(info.framebuffer.type == FramebufferType::DirectRgb, "Expected DirectRgb type");
    return true;
}

// 20. Framebuffer Multiplication Overflow Protection
static bool test_framebuffer_multiplication_overflow() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 8;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 40;
    *reinterpret_cast<uint64_t*>(&buf[16]) = 0xE0000000;
    *reinterpret_cast<uint32_t*>(&buf[24]) = 0xFFFFFFFFU; // Huge pitch causing multiplication overflow
    *reinterpret_cast<uint32_t*>(&buf[28]) = 1024;
    *reinterpret_cast<uint32_t*>(&buf[32]) = 768;
    buf[36] = 32;
    buf[37] = 1;

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!info.framebuffer.valid, "Multiplication overflow must reject framebuffer");
    return true;
}

// 21. Framebuffer Format Payload Too Short
static bool test_framebuffer_format_payload_too_short() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 8;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 34; // size = 34 (< 38 for DirectRgb)
    *reinterpret_cast<uint64_t*>(&buf[16]) = 0xE0000000;
    *reinterpret_cast<uint32_t*>(&buf[24]) = 4096;
    *reinterpret_cast<uint32_t*>(&buf[28]) = 1024;
    *reinterpret_cast<uint32_t*>(&buf[32]) = 768;
    buf[36] = 32;
    buf[37] = 1; // DirectRgb requires size >= 38

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!info.framebuffer.valid, "Incomplete DirectRgb fields must reject framebuffer");
    return true;
}

// 22. ACPI 1.0 RSDP Validation
static bool test_acpi_validation_v1() {
    std::vector<uint8_t> buf(48, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 48; // total_size
    *reinterpret_cast<uint32_t*>(&buf[8]) = 14; // AcpiOld tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 28; // size = 8 + 20

    uint8_t* rsdp = &buf[16];
    memcpy(rsdp, "RSD PTR ", 8);
    memcpy(rsdp + 9, "BOCHS ", 6);
    rsdp[15] = 0; // Revision 0
    *reinterpret_cast<uint32_t*>(rsdp + 16) = 0x07FE0000; // RSDT physical address

    // Compute checksum so sum(rsdp[0..19]) % 256 == 0
    uint32_t sum = 0;
    for (int i = 0; i < 20; ++i) {
        if (i != 8) sum += rsdp[i];
    }
    rsdp[8] = static_cast<uint8_t>((256 - (sum % 256)) % 256);

    // End tag at offset 8 + 32 = 40
    *reinterpret_cast<uint32_t*>(&buf[40]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[44]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.acpi.valid, "Expected acpi.valid == true");
    TEST_ASSERT(!info.acpi.is_v2, "Expected acpi.is_v2 == false");
    TEST_ASSERT(info.acpi.length == 20, "Expected length == 20");
    TEST_ASSERT(info.acpi.rsdt_physical_address == 0x07FE0000, "Expected correct RSDT physical address");
    TEST_ASSERT(strncmp(info.acpi.oem_id, "BOCHS ", 6) == 0, "Expected OEM ID 'BOCHS '");
    return true;
}

// 23. ACPI 2.0 RSDP Validation
static bool test_acpi_validation_v2() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 15; // AcpiNew tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 44; // size = 8 + 36

    uint8_t* rsdp = &buf[16];
    memcpy(rsdp, "RSD PTR ", 8);
    memcpy(rsdp + 9, "ACPI20", 6);
    rsdp[15] = 2; // Revision 2
    *reinterpret_cast<uint32_t*>(rsdp + 16) = 0x07FE0000; // RSDT physical address
    *reinterpret_cast<uint32_t*>(rsdp + 20) = 36; // Length
    *reinterpret_cast<uint64_t*>(rsdp + 24) = 0x07FE1000ULL; // XSDT physical address

    // Compute first 20-byte checksum
    uint32_t sum1 = 0;
    for (int i = 0; i < 20; ++i) {
        if (i != 8) sum1 += rsdp[i];
    }
    rsdp[8] = static_cast<uint8_t>((256 - (sum1 % 256)) % 256);

    // Compute extended 36-byte checksum
    uint32_t sum2 = 0;
    for (int i = 0; i < 36; ++i) {
        if (i != 32) sum2 += rsdp[i];
    }
    rsdp[32] = static_cast<uint8_t>((256 - (sum2 % 256)) % 256);

    *reinterpret_cast<uint32_t*>(&buf[56]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[60]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.acpi.valid, "Expected acpi.valid == true");
    TEST_ASSERT(info.acpi.is_v2, "Expected acpi.is_v2 == true");
    TEST_ASSERT(info.acpi.length == 36, "Expected length == 36");
    TEST_ASSERT(info.acpi.xsdt_physical_address == 0x07FE1000ULL, "Expected correct XSDT address");
    return true;
}

// 24. ACPI Corrupted Checksum Rejection
static bool test_acpi_corrupted_checksum() {
    std::vector<uint8_t> buf(48, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 48;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 14;
    *reinterpret_cast<uint32_t*>(&buf[12]) = 28;

    uint8_t* rsdp = &buf[16];
    memcpy(rsdp, "RSD PTR ", 8);
    rsdp[8] = 0xAA; // Arbitrary bad checksum

    // End tag at offset 8 + 32 = 40
    *reinterpret_cast<uint32_t*>(&buf[40]) = 0;
    *reinterpret_cast<uint32_t*>(&buf[44]) = 8;

    BootInformation info;
    info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!info.acpi.valid, "Corrupted checksum must reject ACPI tag");
    return true;
}

// 25. EFI64 Validation
static bool test_efi64_validation() {
    std::vector<uint8_t> buf(32, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 32;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 12; // Efi64
    *reinterpret_cast<uint32_t*>(&buf[12]) = 16;
    *reinterpret_cast<uint64_t*>(&buf[16]) = 0x1F5EC018ULL; // EFI system table pointer

    *reinterpret_cast<uint32_t*>(&buf[24]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[28]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.efi.present, "Expected efi.present == true");
    TEST_ASSERT(info.efi.valid, "Expected efi.valid == true");
    TEST_ASSERT(info.efi.is_physical, "Expected efi.is_physical == true");
    TEST_ASSERT(info.efi.is_firmware_owned, "Expected efi.is_firmware_owned == true");
    TEST_ASSERT(info.efi.system_table_paddr == 0x1F5EC018ULL, "Expected correct system table address");
    return true;
}

// 26. Command-Line Token & Value Parsing
static bool test_command_line_token_parsing() {
    BootInformation info;

    // Test: Exact "test" token
    strcpy(info.command_line, "test");
    TEST_ASSERT(info.is_test_mode(), "'test' must be test mode");
    TEST_ASSERT(!info.is_debug_mode(), "'test' is not debug mode");
    TEST_ASSERT(info.get_boot_mode() == BootMode::Test, "Expected BootMode::Test");

    // Test: Exact "debug" token
    strcpy(info.command_line, "debug");
    TEST_ASSERT(!info.is_test_mode(), "'debug' is not test mode");
    TEST_ASSERT(info.is_debug_mode(), "'debug' must be debug mode");
    TEST_ASSERT(info.get_boot_mode() == BootMode::Debug, "Expected BootMode::Debug");

    // Negative case: Words containing 't' or 's'
    strcpy(info.command_line, "testingfoo");
    TEST_ASSERT(!info.is_test_mode(), "'testingfoo' must NOT trigger test mode");

    strcpy(info.command_line, "status");
    TEST_ASSERT(!info.is_test_mode(), "'status' must NOT trigger test mode");

    strcpy(info.command_line, "debugger");
    TEST_ASSERT(!info.is_debug_mode(), "'debugger' must NOT trigger debug mode");

    // Normal mode resolution
    strcpy(info.command_line, "console=ttyS0");
    TEST_ASSERT(info.get_boot_mode() == BootMode::Normal, "Expected BootMode::Normal");

    // Test: Multiple arguments with whitespace and key=value extraction
    strcpy(info.command_line, "  root=/dev/sda1   test   console=ttyS0  debug  ");
    TEST_ASSERT(info.is_test_mode(), "Must detect 'test' among multiple arguments");
    TEST_ASSERT(info.is_debug_mode(), "Must detect 'debug' among multiple arguments");
    TEST_ASSERT(info.has_argument("console"), "Must match key=value argument 'console'");
    TEST_ASSERT(!info.has_argument("sda1"), "Must NOT match value substring 'sda1'");

    char val[32];
    TEST_ASSERT(info.get_argument_value("console", val, sizeof(val)), "Should extract console value");
    TEST_ASSERT(strcmp(val, "ttyS0") == 0, "Extracted value should be 'ttyS0'");

    TEST_ASSERT(info.get_argument_value("root", val, sizeof(val)), "Should extract root value");
    TEST_ASSERT(strcmp(val, "/dev/sda1") == 0, "Extracted value should be '/dev/sda1'");

    // Test: Empty command line
    strcpy(info.command_line, "");
    TEST_ASSERT(!info.is_test_mode(), "Empty command line is not test mode");
    TEST_ASSERT(!info.is_debug_mode(), "Empty command line is not debug mode");
    TEST_ASSERT(info.get_boot_mode() == BootMode::Normal, "Empty command line is BootMode::Normal");

    return true;
}

// 27. Memory Reservation Query Sanity
static bool test_memory_reservation_queries() {
    BootInformation info;
    info.mb2_info_paddr = 0x10000;
    info.mb2_info_total_size = 0x2000;

    // Overlapping query on multiboot info
    TEST_ASSERT(info.is_in_multiboot_info(0x11000, 0x1000), "0x11000 must overlap MB2 info [0x10000, 0x12000)");
    TEST_ASSERT(info.is_in_multiboot_info(0x0F000, 0x2000), "0x0F000..0x11000 must overlap MB2 info");
    TEST_ASSERT(!info.is_in_multiboot_info(0x12000, 0x1000), "0x12000 is outside MB2 info");

    // Overflow protection on query parameters
    TEST_ASSERT(!info.is_in_multiboot_info(0xFFFFFFFFFFFFFFFFULL, 10), "Overflowing region must return false");
    TEST_ASSERT(!info.is_in_multiboot_info(0x10000, 0), "Zero-length region must return false");

    return true;
}

int main() {
    printf("================================================================================\n");
    printf(" Running LlamaOS/A Multiboot2 & Parser Regression Tests (Host-Side)\n");
    printf("================================================================================\n");

    struct TestCase {
        const char* name;
        bool (*func)();
    } tests[] = {
        {"test_invalid_magic", test_invalid_magic},
        {"test_zero_info_address", test_zero_info_address},
        {"test_unaligned_info_address", test_unaligned_info_address},
        {"test_too_small_total_size", test_too_small_total_size},
        {"test_overflowing_total_size", test_overflowing_total_size},
        {"test_zero_tag_size", test_zero_tag_size},
        {"test_tag_size_less_than_header", test_tag_size_less_than_header},
        {"test_oversized_tag", test_oversized_tag},
        {"test_malformed_terminator_size", test_malformed_terminator_size},
        {"test_truncated_tag_header", test_truncated_tag_header},
        {"test_unknown_tag_skipped", test_unknown_tag_skipped},
        {"test_duplicate_tags_protection", test_duplicate_tags_protection},
        {"test_unterminated_cmdline", test_unterminated_cmdline},
        {"test_malformed_mmap_entry_size", test_malformed_mmap_entry_size},
        {"test_malformed_mmap_entry_version", test_malformed_mmap_entry_version},
        {"test_mmap_trailing_partial_entry", test_mmap_trailing_partial_entry},
        {"test_mmap_future_larger_entry", test_mmap_future_larger_entry},
        {"test_mmap_usable_ram_overflow", test_mmap_usable_ram_overflow},
        {"test_framebuffer_validation_valid", test_framebuffer_validation_valid},
        {"test_framebuffer_multiplication_overflow", test_framebuffer_multiplication_overflow},
        {"test_framebuffer_format_payload_too_short", test_framebuffer_format_payload_too_short},
        {"test_acpi_validation_v1", test_acpi_validation_v1},
        {"test_acpi_validation_v2", test_acpi_validation_v2},
        {"test_acpi_corrupted_checksum", test_acpi_corrupted_checksum},
        {"test_efi64_validation", test_efi64_validation},
        {"test_command_line_token_parsing", test_command_line_token_parsing},
        {"test_memory_reservation_queries", test_memory_reservation_queries},
    };

    int passed = 0;
    int failed = 0;
    for (const auto& t : tests) {
        printf(" [RUN] %-42s ... ", t.name);
        if (t.func()) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED\n");
            failed++;
        }
    }

    printf("================================================================================\n");
    printf(" Regression Test Summary: %d Passed, %d Failed\n", passed, failed);
    printf("================================================================================\n");

    return failed == 0 ? 0 : 1;
}
