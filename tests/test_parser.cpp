#include "arch/x86_64/boot/multiboot2.hpp"
#include "core/string.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

// =============================================================================
// LlamaOS/A - Multiboot2 Parser & Command-Line Regression Test Suite
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

// 9. Unterminated Cmdline String
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

// 10. Malformed Memory Map Entry Size
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

// 11. Memory Map Usable RAM Calculation & Saturation
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

// 12. Framebuffer Dimension and Size Validation
static bool test_framebuffer_validation() {
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
    TEST_ASSERT(info.framebuffer.type == FramebufferType::DirectRgb, "Expected DirectRgb type");
    return true;
}

// 13. ACPI 1.0 RSDP Checksum & Signature Validation
static bool test_acpi_validation() {
    std::vector<uint8_t> buf(64, 0);
    *reinterpret_cast<uint32_t*>(&buf[0]) = 64;
    *reinterpret_cast<uint32_t*>(&buf[8]) = 14; // AcpiOld tag
    *reinterpret_cast<uint32_t*>(&buf[12]) = 28; // size = 8 + 20

    uint8_t* rsdp = &buf[16];
    memcpy(rsdp, "RSD PTR ", 8);
    memcpy(rsdp + 9, "BOCHS ", 6);
    rsdp[15] = 0; // Revision 0

    // Compute checksum so sum(rsdp[0..19]) % 256 == 0
    uint32_t sum = 0;
    for (int i = 0; i < 20; ++i) {
        if (i != 8) sum += rsdp[i];
    }
    rsdp[8] = static_cast<uint8_t>((256 - (sum % 256)) % 256);

    *reinterpret_cast<uint32_t*>(&buf[48]) = 0; // End tag
    *reinterpret_cast<uint32_t*>(&buf[52]) = 8;

    BootInformation info;
    bool res = info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(res, "Parse should succeed");
    TEST_ASSERT(info.acpi.valid, "Expected acpi.valid == true");
    TEST_ASSERT(!info.acpi.is_v2, "Expected acpi.is_v2 == false");
    TEST_ASSERT(strncmp(info.acpi.oem_id, "BOCHS ", 6) == 0, "Expected OEM ID 'BOCHS '");

    // Negative case: corrupt checksum
    rsdp[8] ^= 0xFF;
    BootInformation bad_info;
    bad_info.parse(MULTIBOOT2_BOOTLOADER_MAGIC, reinterpret_cast<uintptr_t>(buf.data()));
    TEST_ASSERT(!bad_info.acpi.valid, "Corrupted checksum must reject ACPI RSDP");

    return true;
}

// 14. Command-Line Token Parsing
static bool test_command_line_tokens() {
    BootInformation info;

    // Test: Exact "test" token
    strcpy(info.command_line, "test");
    TEST_ASSERT(info.is_test_mode(), "'test' must be test mode");
    TEST_ASSERT(!info.is_debug_mode(), "'test' is not debug mode");

    // Test: Exact "debug" token
    strcpy(info.command_line, "debug");
    TEST_ASSERT(!info.is_test_mode(), "'debug' is not test mode");
    TEST_ASSERT(info.is_debug_mode(), "'debug' must be debug mode");

    // Negative case: Words containing 't' or 's'
    strcpy(info.command_line, "testingfoo");
    TEST_ASSERT(!info.is_test_mode(), "'testingfoo' must NOT trigger test mode");

    strcpy(info.command_line, "status");
    TEST_ASSERT(!info.is_test_mode(), "'status' must NOT trigger test mode");

    // Test: Multiple arguments with whitespace
    strcpy(info.command_line, "root=/dev/sda1   test   console=ttyS0  debug");
    TEST_ASSERT(info.is_test_mode(), "Must detect 'test' among multiple arguments");
    TEST_ASSERT(info.is_debug_mode(), "Must detect 'debug' among multiple arguments");
    TEST_ASSERT(info.has_argument("console"), "Must match key=value argument 'console'");
    TEST_ASSERT(!info.has_argument("sda1"), "Must NOT match value substring 'sda1'");

    // Test: Empty command line
    strcpy(info.command_line, "");
    TEST_ASSERT(!info.is_test_mode(), "Empty command line is not test mode");
    TEST_ASSERT(!info.is_debug_mode(), "Empty command line is not debug mode");

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
        {"test_unterminated_cmdline", test_unterminated_cmdline},
        {"test_malformed_mmap_entry_size", test_malformed_mmap_entry_size},
        {"test_mmap_usable_ram_overflow", test_mmap_usable_ram_overflow},
        {"test_framebuffer_validation", test_framebuffer_validation},
        {"test_acpi_validation", test_acpi_validation},
        {"test_command_line_tokens", test_command_line_tokens},
    };

    int passed = 0;
    int failed = 0;
    for (const auto& t : tests) {
        printf(" [RUN] %-36s ... ", t.name);
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
