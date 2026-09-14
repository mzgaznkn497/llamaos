#include "storage/partition.hpp"
#include "core/string.hpp"

namespace llamaos::storage {

namespace {

static char hex_digit(uint8_t nibble) {
    nibble &= 0x0F;
    return (nibble < 10) ? static_cast<char>('0' + nibble) : static_cast<char>('A' + (nibble - 10));
}

} // anonymous namespace

void PartitionGuid::format(char* out, size_t max_len) const noexcept {
    if (!out || max_len < 37) {
        if (out && max_len > 0) out[0] = '\0';
        return;
    }

    // Little-endian parts for standard GUID layout:
    // bytes 0..3 (time_low, LE)
    // bytes 4..5 (time_mid, LE)
    // bytes 6..7 (time_hi, LE)
    // bytes 8..9 (clock_seq, BE)
    // bytes 10..15 (node, BE)
    size_t idx = 0;
    auto append_byte = [&](uint8_t b) {
        out[idx++] = hex_digit(b >> 4);
        out[idx++] = hex_digit(b & 0x0F);
    };

    append_byte(bytes[3]); append_byte(bytes[2]); append_byte(bytes[1]); append_byte(bytes[0]);
    out[idx++] = '-';
    append_byte(bytes[5]); append_byte(bytes[4]);
    out[idx++] = '-';
    append_byte(bytes[7]); append_byte(bytes[6]);
    out[idx++] = '-';
    append_byte(bytes[8]); append_byte(bytes[9]);
    out[idx++] = '-';
    for (size_t i = 10; i < 16; ++i) {
        append_byte(bytes[i]);
    }
    out[idx] = '\0';
}

Partition::Partition(BlockDevice* parent,
                     uint32_t partition_index,
                     uint64_t start_lba,
                     uint64_t sector_count,
                     const char* name,
                     const PartitionGuid& type_guid,
                     const PartitionGuid& unique_guid) noexcept
    : m_parent(parent),
      m_partition_index(partition_index),
      m_start_lba(start_lba),
      m_sector_count(sector_count),
      m_device_id(parent ? (parent->device_id() * 100 + partition_index) : partition_index),
      m_type_guid(type_guid),
      m_unique_guid(unique_guid) {
    if (name) {
        llamaos::strncpy(m_name, name, sizeof(m_name) - 1);
        m_name[sizeof(m_name) - 1] = '\0';
    } else {
        llamaos::strncpy(m_name, "part", sizeof(m_name) - 1);
    }
}

BlockStatus Partition::read_sectors(uint64_t lba, uint32_t count, void* dst) {
    if (!m_parent) return BlockStatus::DeviceFault;
    if (!validate_bounds(lba, count, dst, false)) {
        return BlockStatus::OutOfBounds;
    }

    uint64_t parent_lba = m_start_lba + lba;
    return m_parent->read_sectors(parent_lba, count, dst);
}

BlockStatus Partition::write_sectors(uint64_t lba, uint32_t count, const void* src) {
    if (!m_parent) return BlockStatus::DeviceFault;
    if (!validate_bounds(lba, count, src, true)) {
        return is_read_only() ? BlockStatus::ReadOnly : BlockStatus::OutOfBounds;
    }

    uint64_t parent_lba = m_start_lba + lba;
    return m_parent->write_sectors(parent_lba, count, src);
}

BlockStatus Partition::flush() {
    if (!m_parent) return BlockStatus::DeviceFault;
    return m_parent->flush();
}

} // namespace llamaos::storage
