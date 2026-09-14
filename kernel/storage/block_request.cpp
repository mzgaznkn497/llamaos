#include "storage/block_request.hpp"

namespace llamaos::storage {

const char* to_string(BlockStatus status) noexcept {
    switch (status) {
    case BlockStatus::Success:          return "Success";
    case BlockStatus::Pending:          return "Pending";
    case BlockStatus::IoError:          return "I/O Error";
    case BlockStatus::InvalidParameter: return "Invalid Parameter";
    case BlockStatus::OutOfBounds:      return "Out Of Bounds";
    case BlockStatus::ReadOnly:         return "Read Only";
    case BlockStatus::DeviceFault:      return "Device Fault";
    case BlockStatus::Timeout:          return "Timeout";
    case BlockStatus::Unsupported:      return "Unsupported";
    default:                            return "Unknown";
    }
}

const char* to_string(BlockRequestType type) noexcept {
    switch (type) {
    case BlockRequestType::Read:  return "Read";
    case BlockRequestType::Write: return "Write";
    case BlockRequestType::Flush: return "Flush";
    default:                      return "Unknown";
    }
}

} // namespace llamaos::storage
