#include "storage/block_device.hpp"

namespace llamaos::storage {

bool BlockDevice::validate_bounds(uint64_t lba, uint32_t count, const void* buffer, bool write) const noexcept {
    if (!buffer) return false;
    if (count == 0) return true; // Zero count is a no-op, technically valid or rejected by caller
    if (write && is_read_only()) return false;

    // Integer overflow protection: lba + count
    if (lba > UINT64_MAX - count) return false;

    uint64_t end_lba = lba + count;
    if (end_lba > total_sectors()) return false;

    return true;
}

BlockStatus BlockDevice::submit_request(BlockRequest& req) {
    if (req.sector_count == 0 && req.type != BlockRequestType::Flush) {
        req.mark_completed(BlockStatus::Success);
        return BlockStatus::Success;
    }

    switch (req.type) {
    case BlockRequestType::Read: {
        if (!validate_bounds(req.lba, req.sector_count, req.buffer, false)) {
            req.mark_completed(BlockStatus::OutOfBounds);
            return BlockStatus::OutOfBounds;
        }
        BlockStatus status = read_sectors(req.lba, req.sector_count, req.buffer);
        req.mark_completed(status);
        return status;
    }
    case BlockRequestType::Write: {
        if (!validate_bounds(req.lba, req.sector_count, req.buffer, true)) {
            req.mark_completed(is_read_only() ? BlockStatus::ReadOnly : BlockStatus::OutOfBounds);
            return req.status;
        }
        BlockStatus status = write_sectors(req.lba, req.sector_count, req.buffer);
        req.mark_completed(status);
        return status;
    }
    case BlockRequestType::Flush: {
        BlockStatus status = flush();
        req.mark_completed(status);
        return status;
    }
    default:
        req.mark_completed(BlockStatus::InvalidParameter);
        return BlockStatus::InvalidParameter;
    }
}

} // namespace llamaos::storage
