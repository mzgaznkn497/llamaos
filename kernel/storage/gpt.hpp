#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"
#include "storage/partition.hpp"

// =============================================================================
// LlamaOS/A - GUID Partition Table (GPT) Parser & Validator
// =============================================================================
// Implements UEFI GPT specification parsing, protective MBR verification,
// CRC32 checksum calculation, partition boundary validation, and automatic
// partition block device registration.
// =============================================================================

namespace llamaos::storage {

// CRC32 IEEE 802.3 checksum calculation helper
uint32_t calculate_crc32(const void* data, size_t length, uint32_t seed = 0) noexcept;

struct [[gnu::packed]] GptHeader {
    uint64_t signature;                     // "EFI PART" = 0x5452415020494645ULL
    uint32_t revision;                      // 0x00010000 for GPT 1.0
    uint32_t header_size;                   // Typically 92 bytes
    uint32_t header_crc32;                  // CRC32 of header with this field zeroed
    uint32_t reserved0;
    uint64_t my_lba;                        // Current LBA (1 for primary)
    uint64_t alternate_lba;                 // Backup LBA
    uint64_t first_usable_lba;              // First usable LBA for partitions
    uint64_t last_usable_lba;               // Last usable LBA for partitions
    PartitionGuid disk_guid;                // Unique disk GUID
    uint64_t partition_entry_lba;           // Starting LBA of partition entries (usually 2)
    uint32_t number_of_partition_entries;   // Number of entries (typically 128)
    uint32_t size_of_partition_entry;       // Size of each entry (typically 128)
    uint32_t partition_entry_array_crc32;   // CRC32 of partition entry array
};

static_assert(sizeof(GptHeader) == 92, "GptHeader must be exactly 92 bytes");

struct [[gnu::packed]] GptEntry {
    PartitionGuid type_guid;                // Partition type GUID
    PartitionGuid unique_partition_guid;    // Unique partition GUID
    uint64_t      starting_lba;             // Starting LBA
    uint64_t      ending_lba;               // Ending LBA (inclusive)
    uint64_t      attributes;               // Attribute flags
    uint16_t      partition_name[36];       // Partition name (UTF-16LE, 72 bytes)
};

static_assert(sizeof(GptEntry) == 128, "GptEntry must be exactly 128 bytes");

// Well-known partition type GUIDs
inline constexpr PartitionGuid GUID_BIOS_BOOT = {
    {0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E, 0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49} // 21686148-6449-6E6F-744E-656564454649
};

inline constexpr PartitionGuid GUID_EFI_SYSTEM = {
    {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B} // C12A7328-F81F-11D2-BA4B-00A0C93EC93B
};

inline constexpr PartitionGuid GUID_LINUX_FS = {
    {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4} // 0FC63DAF-8483-4772-8E79-3D69D8477DE4
};

inline constexpr PartitionGuid GUID_BASIC_DATA = {
    {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7} // EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
};

enum class GptStatus {
    Success,
    IoError,
    InvalidSignature,
    InvalidHeaderCrc,
    InvalidArrayCrc,
    InvalidUsableRange,
    InvalidEntrySize,
    OverlappingPartitions,
    PartitionOutOfBounds,
    NoPartitionsFound
};

[[nodiscard]] const char* to_string(GptStatus status) noexcept;

class GptParser {
public:
    static constexpr uint64_t GPT_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"
    static constexpr size_t   MAX_PARSED_PARTITIONS = 16;

    // Parses GPT from disk, validates CRC32 and partition bounds, and registers partitions
    static GptStatus parse_and_register(BlockDevice* disk, size_t* out_partition_count = nullptr);

    // Validates in-memory GPT structures (pure function, host unit-testable)
    static GptStatus validate_gpt(const GptHeader& header,
                                  const GptEntry* entries,
                                  size_t entry_count,
                                  uint64_t disk_total_sectors) noexcept;

private:
    static Partition s_partitions[MAX_PARSED_PARTITIONS];
    static size_t    s_partition_count;
};

} // namespace llamaos::storage
