#pragma once

#include "core/types.hpp"

// =============================================================================
// LlamaOS/A - Block Request & Status Definitions
// =============================================================================
// Encapsulates asynchronous or synchronous block I/O requests (read, write, flush)
// submitted to a block device.
// =============================================================================

namespace llamaos::storage {

enum class BlockRequestType : uint8_t {
    Read,
    Write,
    Flush
};

enum class BlockStatus : uint8_t {
    Success,
    Pending,
    IoError,
    InvalidParameter,
    OutOfBounds,
    ReadOnly,
    DeviceFault,
    Timeout,
    Unsupported
};

[[nodiscard]] const char* to_string(BlockStatus status) noexcept;
[[nodiscard]] const char* to_string(BlockRequestType type) noexcept;

struct BlockRequest {
    BlockRequestType type{BlockRequestType::Read};
    uint64_t         lba{0};
    uint32_t         sector_count{0};
    uint8_t*         buffer{nullptr};
    size_t           buffer_size{0};
    BlockStatus      status{BlockStatus::Pending};
    bool             completed{false};

    [[nodiscard]] constexpr bool is_read() const noexcept {
        return type == BlockRequestType::Read;
    }

    [[nodiscard]] constexpr bool is_write() const noexcept {
        return type == BlockRequestType::Write;
    }

    [[nodiscard]] constexpr bool is_flush() const noexcept {
        return type == BlockRequestType::Flush;
    }

    void mark_completed(BlockStatus s) noexcept {
        status = s;
        completed = true;
    }
};

} // namespace llamaos::storage
