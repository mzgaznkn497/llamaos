#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"

// =============================================================================
// LlamaOS/A - Storage Partition Abstraction
// =============================================================================
// Implements a logical BlockDevice sub-range representing a disk partition
// (MBR or GPT). Translates partition-relative sector offsets to parent disk LBAs
// and strictly prevents out-of-partition access.
// =============================================================================

namespace llamaos::storage {

struct [[gnu::packed]] PartitionGuid {
    uint8_t bytes[16]{0};

    [[nodiscard]] bool is_null() const noexcept {
        for (uint8_t b : bytes) {
            if (b != 0) return false;
        }
        return true;
    }

    bool operator==(const PartitionGuid& other) const noexcept {
        for (size_t i = 0; i < 16; ++i) {
            if (bytes[i] != other.bytes[i]) return false;
        }
        return true;
    }

    bool operator!=(const PartitionGuid& other) const noexcept {
        return !(*this == other);
    }

    // Formats GUID as "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" (37 chars including null)
    void format(char* out, size_t max_len) const noexcept;
};

class Partition : public BlockDevice {
public:
    Partition(BlockDevice* parent,
              uint32_t partition_index,
              uint64_t start_lba,
              uint64_t sector_count,
              const char* name,
              const PartitionGuid& type_guid = {},
              const PartitionGuid& unique_guid = {}) noexcept;

    // BlockDevice interface implementation
    [[nodiscard]] const char* name() const noexcept override { return m_name; }
    [[nodiscard]] uint32_t device_id() const noexcept override { return m_device_id; }
    [[nodiscard]] uint32_t sector_size() const noexcept override {
        return m_parent ? m_parent->sector_size() : 512;
    }
    [[nodiscard]] uint64_t total_sectors() const noexcept override { return m_sector_count; }
    [[nodiscard]] bool is_read_only() const noexcept override {
        return m_parent ? m_parent->is_read_only() : true;
    }

    BlockStatus read_sectors(uint64_t lba, uint32_t count, void* dst) override;
    BlockStatus write_sectors(uint64_t lba, uint32_t count, const void* src) override;
    BlockStatus flush() override;

    // Partition-specific inquiries
    [[nodiscard]] BlockDevice* parent() const noexcept { return m_parent; }
    [[nodiscard]] uint32_t partition_index() const noexcept { return m_partition_index; }
    [[nodiscard]] uint64_t start_lba() const noexcept { return m_start_lba; }
    [[nodiscard]] const PartitionGuid& type_guid() const noexcept { return m_type_guid; }
    [[nodiscard]] const PartitionGuid& unique_guid() const noexcept { return m_unique_guid; }

private:
    BlockDevice*  m_parent{nullptr};
    uint32_t      m_partition_index{0};
    uint64_t      m_start_lba{0};
    uint64_t      m_sector_count{0};
    uint32_t      m_device_id{0};
    char          m_name[NAME_MAX_LEN]{0};
    PartitionGuid m_type_guid{};
    PartitionGuid m_unique_guid{};
};

} // namespace llamaos::storage
