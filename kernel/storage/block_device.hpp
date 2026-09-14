#pragma once

#include "core/types.hpp"
#include "storage/block_request.hpp"

// =============================================================================
// LlamaOS/A - Block Device Abstraction
// =============================================================================
// Pure abstract interface for physical or virtual block storage devices
// (VirtIO-blk, NVMe, IDE, partitions, RAM disks).
// Enforces bounds checking, sector size compliance, and error propagation.
// =============================================================================

namespace llamaos::storage {

class BlockDevice {
public:
    static constexpr size_t NAME_MAX_LEN = 32;

    virtual ~BlockDevice() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual uint32_t device_id() const noexcept = 0;
    [[nodiscard]] virtual uint32_t sector_size() const noexcept = 0;
    [[nodiscard]] virtual uint64_t total_sectors() const noexcept = 0;
    [[nodiscard]] virtual uint64_t capacity_bytes() const noexcept {
        return total_sectors() * static_cast<uint64_t>(sector_size());
    }
    [[nodiscard]] virtual bool is_read_only() const noexcept = 0;

    // Core synchronous I/O operations
    virtual BlockStatus read_sectors(uint64_t lba, uint32_t count, void* dst) = 0;
    virtual BlockStatus write_sectors(uint64_t lba, uint32_t count, const void* src) = 0;
    virtual BlockStatus flush() = 0;

    // Asynchronous / request-based entry point
    virtual BlockStatus submit_request(BlockRequest& req);

    // Bounds & argument verification helper
    [[nodiscard]] bool validate_bounds(uint64_t lba, uint32_t count, const void* buffer, bool write) const noexcept;
};

} // namespace llamaos::storage
