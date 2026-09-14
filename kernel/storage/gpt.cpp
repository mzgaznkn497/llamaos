#include "storage/gpt.hpp"
#include "storage/storage_manager.hpp"
#include "memory/kalloc.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

namespace llamaos::storage {

Partition GptParser::s_partitions[MAX_PARSED_PARTITIONS]{
    {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""},
    {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""},
    {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""},
    {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}, {nullptr, 0, 0, 0, ""}
};
size_t GptParser::s_partition_count{0};

uint32_t calculate_crc32(const void* data, size_t length, uint32_t seed) noexcept {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = ~seed;

    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

const char* to_string(GptStatus status) noexcept {
    switch (status) {
    case GptStatus::Success:               return "Success";
    case GptStatus::IoError:               return "I/O Error";
    case GptStatus::InvalidSignature:      return "Invalid Signature";
    case GptStatus::InvalidHeaderCrc:      return "Invalid Header CRC32";
    case GptStatus::InvalidArrayCrc:       return "Invalid Partition Array CRC32";
    case GptStatus::InvalidUsableRange:    return "Invalid Usable LBA Range";
    case GptStatus::InvalidEntrySize:      return "Invalid Entry Size";
    case GptStatus::OverlappingPartitions: return "Overlapping Partitions";
    case GptStatus::PartitionOutOfBounds:  return "Partition Out Of Bounds";
    case GptStatus::NoPartitionsFound:     return "No Partitions Found";
    default:                               return "Unknown";
    }
}

GptStatus GptParser::validate_gpt(const GptHeader& header,
                                  const GptEntry* entries,
                                  size_t entry_count,
                                  uint64_t disk_total_sectors) noexcept {
    // 1. Signature verification ("EFI PART")
    if (header.signature != GPT_SIGNATURE) {
        return GptStatus::InvalidSignature;
    }

    // 2. Header size sanity
    if (header.header_size < sizeof(GptHeader) || header.header_size > 512) {
        return GptStatus::InvalidHeaderCrc;
    }

    // 3. Header CRC32 verification
    GptHeader copy = header;
    copy.header_crc32 = 0;
    uint32_t computed_header_crc = calculate_crc32(&copy, header.header_size);
    if (computed_header_crc != header.header_crc32) {
        return GptStatus::InvalidHeaderCrc;
    }

    // 4. Usable LBA range sanity
    if (header.first_usable_lba > header.last_usable_lba ||
        (disk_total_sectors > 0 && header.last_usable_lba >= disk_total_sectors)) {
        return GptStatus::InvalidUsableRange;
    }

    // 5. Partition array sanity
    if (header.size_of_partition_entry < sizeof(GptEntry) ||
        header.number_of_partition_entries > 1024) {
        return GptStatus::InvalidEntrySize;
    }

    // 6. Partition array CRC32 verification (if entries provided)
    if (entries && entry_count >= header.number_of_partition_entries) {
        size_t array_bytes = header.number_of_partition_entries * header.size_of_partition_entry;
        uint32_t computed_array_crc = calculate_crc32(entries, array_bytes);
        if (computed_array_crc != header.partition_entry_array_crc32) {
            return GptStatus::InvalidArrayCrc;
        }

        // 7. Individual entry bounds and overlap checking
        for (size_t i = 0; i < header.number_of_partition_entries; ++i) {
            const GptEntry& e1 = entries[i];
            if (e1.type_guid.is_null()) continue;

            if (e1.starting_lba > e1.ending_lba) {
                return GptStatus::PartitionOutOfBounds;
            }

            if (e1.starting_lba < header.first_usable_lba ||
                e1.ending_lba > header.last_usable_lba) {
                return GptStatus::PartitionOutOfBounds;
            }

            // Check for overlap against previously validated active entries
            for (size_t j = 0; j < i; ++j) {
                const GptEntry& e2 = entries[j];
                if (e2.type_guid.is_null()) continue;

                bool overlap = !(e1.ending_lba < e2.starting_lba || e2.ending_lba < e1.starting_lba);
                if (overlap) {
                    return GptStatus::OverlappingPartitions;
                }
            }
        }
    }

    return GptStatus::Success;
}

GptStatus GptParser::parse_and_register(BlockDevice* disk, size_t* out_partition_count) {
    if (!disk) return GptStatus::IoError;

    // 1. Read LBA 1 (Primary GPT Header)
    alignas(512) uint8_t sector_buf[512];
    BlockStatus st = disk->read_sectors(1, 1, sector_buf);
    if (st != BlockStatus::Success) {
        klog_error("GptParser: Failed to read LBA 1 from disk '%s' (status: %s)",
                   disk->name(), to_string(st));
        return GptStatus::IoError;
    }

    const auto* header = reinterpret_cast<const GptHeader*>(sector_buf);

    // Initial signature check
    if (header->signature != GPT_SIGNATURE) {
        return GptStatus::InvalidSignature;
    }

    // 2. Calculate sectors required for partition entries
    uint32_t num_entries = header->number_of_partition_entries;
    uint32_t entry_size = header->size_of_partition_entry;
    if (num_entries == 0 || entry_size < sizeof(GptEntry) || num_entries > 256) {
        return GptStatus::InvalidEntrySize;
    }

    size_t array_bytes = num_entries * entry_size;
    uint32_t entry_sectors = static_cast<uint32_t>((array_bytes + 511) / 512);

    void* array_mem = memory::kmalloc(entry_sectors * 512);
    if (!array_mem) {
        return GptStatus::IoError;
    }

    st = disk->read_sectors(header->partition_entry_lba, entry_sectors, array_mem);
    if (st != BlockStatus::Success) {
        memory::kfree(array_mem);
        return GptStatus::IoError;
    }

    const auto* entries = static_cast<const GptEntry*>(array_mem);

    // 3. Full validation
    GptStatus val_status = validate_gpt(*header, entries, num_entries, disk->total_sectors());
    if (val_status != GptStatus::Success) {
        klog_error("GptParser: Validation failed on disk '%s': %s",
                   disk->name(), to_string(val_status));
        memory::kfree(array_mem);
        return val_status;
    }

    // 4. Instantiate and register partitions
    size_t registered = 0;
    for (size_t i = 0; i < num_entries && s_partition_count < MAX_PARSED_PARTITIONS; ++i) {
        const GptEntry& entry = entries[i];
        if (entry.type_guid.is_null()) continue;

        uint64_t start = entry.starting_lba;
        uint64_t count = entry.ending_lba - entry.starting_lba + 1;
        uint32_t part_idx = static_cast<uint32_t>(i + 1);

        char name[BlockDevice::NAME_MAX_LEN]{0};
        // Build name: e.g. "vda1", "vda2", "vda15"
        size_t dlen = llamaos::strlen(disk->name());
        llamaos::strncpy(name, disk->name(), sizeof(name) - 8);
        char num_str[16];
        size_t nlen = 0;
        uint32_t temp = part_idx;
        do {
            num_str[nlen++] = static_cast<char>('0' + (temp % 10));
            temp /= 10;
        } while (temp > 0);
        // reverse
        for (size_t r = 0; r < nlen; ++r) {
            name[dlen + r] = num_str[nlen - 1 - r];
        }
        name[dlen + nlen] = '\0';

        Partition& p = s_partitions[s_partition_count];
        p = Partition(disk, part_idx, start, count, name, entry.type_guid, entry.unique_partition_guid);

        char guid_str[40];
        entry.type_guid.format(guid_str, sizeof(guid_str));

        klog_info("GptParser: Discovered partition '%s': LBA [%llu - %llu] (%llu MiB, Type: %s)",
                  name, start, entry.ending_lba, p.capacity_bytes() / (1024 * 1024), guid_str);

        StorageManager::register_device(&p);
        s_partition_count++;
        registered++;
    }

    memory::kfree(array_mem);

    if (out_partition_count) {
        *out_partition_count = registered;
    }

    return (registered > 0) ? GptStatus::Success : GptStatus::NoPartitionsFound;
}

} // namespace llamaos::storage
