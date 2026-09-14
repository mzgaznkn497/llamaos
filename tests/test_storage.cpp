#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "storage/block_request.hpp"
#include "storage/block_device.hpp"
#include "storage/partition.hpp"
#include "storage/storage_manager.hpp"
#include "storage/gpt.hpp"
#include "fs/vfs.hpp"
#include "fs/fat32.hpp"

// Host stubs for freestanding kernel functions
namespace llamaos {
void klog_info(const char* /*fmt*/, ...) {}
void klog_warn(const char* /*fmt*/, ...) {}
void klog_error(const char* /*fmt*/, ...) {}
}

namespace llamaos::memory {
void* kmalloc(size_t size) { return std::malloc(size); }
void kfree(void* ptr) { std::free(ptr); }
}

using namespace llamaos;
using namespace llamaos::storage;
using namespace llamaos::fs;

// In-memory mock block device for unit testing
class MockBlockDevice : public BlockDevice {
public:
    MockBlockDevice(const char* name, uint32_t id, uint64_t sectors, bool ro = false)
        : m_name(name), m_id(id), m_sectors(sectors), m_ro(ro) {
        m_storage = new uint8_t[sectors * 512];
        std::memset(m_storage, 0, sectors * 512);
    }

    ~MockBlockDevice() override {
        delete[] m_storage;
    }

    const char* name() const noexcept override { return m_name; }
    uint32_t device_id() const noexcept override { return m_id; }
    uint32_t sector_size() const noexcept override { return 512; }
    uint64_t total_sectors() const noexcept override { return m_sectors; }
    bool is_read_only() const noexcept override { return m_ro; }

    BlockStatus read_sectors(uint64_t lba, uint32_t count, void* dst) override {
        if (!validate_bounds(lba, count, dst, false)) return BlockStatus::OutOfBounds;
        std::memcpy(dst, m_storage + (lba * 512), count * 512);
        return BlockStatus::Success;
    }

    BlockStatus write_sectors(uint64_t lba, uint32_t count, const void* src) override {
        if (m_ro) return BlockStatus::ReadOnly;
        if (!validate_bounds(lba, count, src, true)) return BlockStatus::OutOfBounds;
        std::memcpy(m_storage + (lba * 512), src, count * 512);
        return BlockStatus::Success;
    }

    BlockStatus flush() override {
        return BlockStatus::Success;
    }

    uint8_t* raw_storage() { return m_storage; }

private:
    const char* m_name;
    uint32_t m_id;
    uint64_t m_sectors;
    bool m_ro;
    uint8_t* m_storage;
};

void test_block_requests_and_bounds() {
    std::printf("[TEST] Running block request and bounds tests...\n");

    MockBlockDevice disk("test_disk", 1, 1000, false);
    assert(disk.total_sectors() == 1000);
    assert(disk.capacity_bytes() == 1000 * 512);
    assert(!disk.is_read_only());

    uint8_t buf[1024];
    std::memset(buf, 0xAB, sizeof(buf));

    // Bounds check tests
    assert(disk.validate_bounds(0, 2, buf, false));
    assert(disk.validate_bounds(998, 2, buf, false));
    assert(!disk.validate_bounds(999, 2, buf, false)); // Out of bounds
    assert(!disk.validate_bounds(1000, 1, buf, false)); // Out of bounds
    assert(!disk.validate_bounds(UINT64_MAX, 1, buf, false)); // Overflow
    assert(!disk.validate_bounds(0, 2, nullptr, false)); // Null buffer

    // Request submission
    BlockRequest req;
    req.type = BlockRequestType::Write;
    req.lba = 10;
    req.sector_count = 2;
    req.buffer = buf;
    req.buffer_size = sizeof(buf);

    BlockStatus st = disk.submit_request(req);
    assert(st == BlockStatus::Success);
    assert(req.completed);
    assert(req.status == BlockStatus::Success);

    // Read back and verify
    uint8_t read_buf[1024];
    std::memset(read_buf, 0, sizeof(read_buf));
    BlockRequest rreq;
    rreq.type = BlockRequestType::Read;
    rreq.lba = 10;
    rreq.sector_count = 2;
    rreq.buffer = read_buf;
    rreq.buffer_size = sizeof(read_buf);

    st = disk.submit_request(rreq);
    assert(st == BlockStatus::Success);
    assert(std::memcmp(buf, read_buf, 1024) == 0);

    // Read-only rejection test
    MockBlockDevice ro_disk("ro_disk", 2, 500, true);
    assert(ro_disk.is_read_only());
    st = ro_disk.write_sectors(0, 1, buf);
    assert(st == BlockStatus::ReadOnly);

    std::printf("  [PASS] Block request and bounds validation verified.\n");
}

void test_partition_translation() {
    std::printf("[TEST] Running partition translation tests...\n");

    MockBlockDevice disk("parent_disk", 1, 2048, false);

    // Create partition at LBA 100 with 500 sectors
    Partition part(&disk, 1, 100, 500, "parent_disk1");
    assert(part.start_lba() == 100);
    assert(part.total_sectors() == 500);
    assert(part.capacity_bytes() == 500 * 512);

    uint8_t pattern[512];
    std::memset(pattern, 0x42, sizeof(pattern));

    // Write to relative sector 5 of partition -> should write to sector 105 of parent
    BlockStatus st = part.write_sectors(5, 1, pattern);
    assert(st == BlockStatus::Success);

    // Verify parent sector 105 has pattern
    uint8_t parent_check[512];
    st = disk.read_sectors(105, 1, parent_check);
    assert(st == BlockStatus::Success);
    assert(std::memcmp(pattern, parent_check, 512) == 0);

    // Partition out of bounds check
    st = part.read_sectors(500, 1, pattern);
    assert(st == BlockStatus::OutOfBounds);

    std::printf("  [PASS] Partition offset translation and bounds enforcement verified.\n");
}

void test_storage_manager() {
    std::printf("[TEST] Running StorageManager registry tests...\n");

    StorageManager::init();
    assert(StorageManager::device_count() == 0);

    MockBlockDevice d1("vda", 100, 1000);
    MockBlockDevice d2("vdb", 101, 2000);

    assert(StorageManager::register_device(&d1));
    assert(StorageManager::register_device(&d2));
    assert(StorageManager::device_count() == 2);

    // Duplicate registration rejected
    assert(!StorageManager::register_device(&d1));
    assert(StorageManager::device_count() == 2);

    // Lookup
    assert(StorageManager::find_device("vda") == &d1);
    assert(StorageManager::find_device("vdb") == &d2);
    assert(StorageManager::find_device("vdc") == nullptr);
    assert(StorageManager::find_device_by_id(100) == &d1);
    assert(StorageManager::default_boot_device() == &d1);

    // Unregister
    assert(StorageManager::unregister_device(&d1));
    assert(StorageManager::device_count() == 1);
    assert(StorageManager::find_device("vda") == nullptr);

    std::printf("  [PASS] StorageManager registration and lookup verified.\n");
}

void test_gpt_validation() {
    std::printf("[TEST] Running GPT parser & validation tests...\n");

    // Construct valid GPT header and entries
    GptHeader hdr{};
    hdr.signature = GptParser::GPT_SIGNATURE;
    hdr.revision = 0x00010000;
    hdr.header_size = 92;
    hdr.my_lba = 1;
    hdr.alternate_lba = 1999;
    hdr.first_usable_lba = 34;
    hdr.last_usable_lba = 1966;
    hdr.partition_entry_lba = 2;
    hdr.number_of_partition_entries = 4;
    hdr.size_of_partition_entry = 128;

    GptEntry entries[4]{};

    // Entry 0: BIOS Boot (LBA 34..100)
    entries[0].type_guid = GUID_BIOS_BOOT;
    entries[0].starting_lba = 34;
    entries[0].ending_lba = 100;

    // Entry 1: ESP (LBA 101..500)
    entries[1].type_guid = GUID_EFI_SYSTEM;
    entries[1].starting_lba = 101;
    entries[1].ending_lba = 500;

    // Entry 2: Linux FS (LBA 501..1500)
    entries[2].type_guid = GUID_LINUX_FS;
    entries[2].starting_lba = 501;
    entries[2].ending_lba = 1500;

    // Entry 3: Unused (all zero GUID)
    entries[3].starting_lba = 0;
    entries[3].ending_lba = 0;

    // Compute CRCs
    hdr.partition_entry_array_crc32 = calculate_crc32(entries, sizeof(entries));
    hdr.header_crc32 = 0;
    hdr.header_crc32 = calculate_crc32(&hdr, hdr.header_size);

    // Test 1: Valid GPT
    GptStatus st = GptParser::validate_gpt(hdr, entries, 4, 2000);
    assert(st == GptStatus::Success);

    // Test 2: Invalid Signature
    GptHeader bad_sig = hdr;
    bad_sig.signature = 0x12345678;
    st = GptParser::validate_gpt(bad_sig, entries, 4, 2000);
    assert(st == GptStatus::InvalidSignature);

    // Test 3: Invalid Header CRC
    GptHeader bad_crc = hdr;
    bad_crc.header_crc32 ^= 0xFFFFFFFF;
    st = GptParser::validate_gpt(bad_crc, entries, 4, 2000);
    assert(st == GptStatus::InvalidHeaderCrc);

    // Test 4: Invalid Partition Array CRC
    GptHeader bad_array_crc = hdr;
    bad_array_crc.partition_entry_array_crc32 ^= 0x1234;
    bad_array_crc.header_crc32 = 0;
    bad_array_crc.header_crc32 = calculate_crc32(&bad_array_crc, bad_array_crc.header_size);
    st = GptParser::validate_gpt(bad_array_crc, entries, 4, 2000);
    assert(st == GptStatus::InvalidArrayCrc);

    // Test 5: Invalid Usable Range (last_usable_lba > disk total)
    GptHeader out_range = hdr;
    out_range.last_usable_lba = 5000;
    out_range.header_crc32 = 0;
    out_range.header_crc32 = calculate_crc32(&out_range, out_range.header_size);
    st = GptParser::validate_gpt(out_range, entries, 4, 2000);
    assert(st == GptStatus::InvalidUsableRange);

    // Test 6: Overlapping Partitions
    GptEntry overlap_entries[4]{};
    overlap_entries[0] = entries[0]; // LBA 34..100
    overlap_entries[1] = entries[1];
    overlap_entries[1].starting_lba = 80; // Overlaps with entry 0!
    overlap_entries[1].ending_lba = 300;

    GptHeader overlap_hdr = hdr;
    overlap_hdr.partition_entry_array_crc32 = calculate_crc32(overlap_entries, sizeof(overlap_entries));
    overlap_hdr.header_crc32 = 0;
    overlap_hdr.header_crc32 = calculate_crc32(&overlap_hdr, overlap_hdr.header_size);

    st = GptParser::validate_gpt(overlap_hdr, overlap_entries, 4, 2000);
    assert(st == GptStatus::OverlappingPartitions);

    // Test 7: Out-of-bounds Partition (starts before first_usable_lba)
    GptEntry oob_entries[4]{};
    oob_entries[0] = entries[0];
    oob_entries[0].starting_lba = 10; // Before first_usable_lba (34)
    GptHeader oob_hdr = hdr;
    oob_hdr.partition_entry_array_crc32 = calculate_crc32(oob_entries, sizeof(oob_entries));
    oob_hdr.header_crc32 = 0;
    oob_hdr.header_crc32 = calculate_crc32(&oob_hdr, oob_hdr.header_size);

    st = GptParser::validate_gpt(oob_hdr, oob_entries, 4, 2000);
    assert(st == GptStatus::PartitionOutOfBounds);

    std::printf("  [PASS] GPT parser validation (valid, invalid signature, invalid CRC, overlaps, out-of-bounds) verified.\n");
}

void test_vfs_path_normalization() {
    std::printf("[TEST] Running VFS path normalization tests...\n");

    char out[128];

    assert(Vfs::normalize_path("/", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/") == 0);

    assert(Vfs::normalize_path("///", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/") == 0);

    assert(Vfs::normalize_path("/foo/bar", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/foo/bar") == 0);

    assert(Vfs::normalize_path("/foo/./bar", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/foo/bar") == 0);

    assert(Vfs::normalize_path("/foo/bar/..", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/foo") == 0);

    assert(Vfs::normalize_path("/foo/bar/../../baz", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/baz") == 0);

    assert(Vfs::normalize_path("//a//b///c//", out, sizeof(out)) == 0);
    assert(std::strcmp(out, "/a/b/c") == 0);

    std::printf("  [PASS] VFS path normalization and traversal safety verified.\n");
}

void test_fat32_filename_83() {
    std::printf("[TEST] Running FAT32 8.3 filename conversions...\n");

    char out_83[11];
    char restored[64];

    Fat32Filesystem::format_to_83("llamaos.elf", out_83);
    assert(std::memcmp(out_83, "LLAMAOS ELF", 11) == 0);
    Fat32Filesystem::format_from_83(out_83, restored, sizeof(restored));
    assert(std::strcmp(restored, "llamaos.elf") == 0);

    Fat32Filesystem::format_to_83("init", out_83);
    assert(std::memcmp(out_83, "INIT       ", 11) == 0);
    Fat32Filesystem::format_from_83(out_83, restored, sizeof(restored));
    assert(std::strcmp(restored, "init") == 0);

    Fat32Filesystem::format_to_83(".", out_83);
    assert(out_83[0] == '.');
    Fat32Filesystem::format_to_83("..", out_83);
    assert(out_83[0] == '.' && out_83[1] == '.');

    std::printf("  [PASS] FAT32 8.3 filename formatting and bidirectional conversion verified.\n");
}

int main() {
    std::printf("======================================================================\n");
    std::printf(" LlamaOS/A - Storage, GPT, VFS & Filesystem Host Regression Suite\n");
    std::printf("======================================================================\n");

    test_block_requests_and_bounds();
    test_partition_translation();
    test_storage_manager();
    test_gpt_validation();
    test_vfs_path_normalization();
    test_fat32_filename_83();

    std::printf("\n>>> ALL STORAGE, GPT & FILESYSTEM HOST TESTS PASSED (6/6) <<<\n");
    return 0;
}
