#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"

// =============================================================================
// LlamaOS/A - Storage Manager & Device Registry
// =============================================================================
// Central registry for physical block devices and disk partitions.
// Manages device enumeration, device lookup, and boot disk selection.
// =============================================================================

namespace llamaos::storage {

class StorageManager {
public:
    static constexpr size_t MAX_BLOCK_DEVICES = 32;

    static void init();

    static bool register_device(BlockDevice* dev);
    static bool unregister_device(BlockDevice* dev);

    [[nodiscard]] static size_t device_count() noexcept;
    [[nodiscard]] static BlockDevice* get_device(size_t index) noexcept;
    [[nodiscard]] static BlockDevice* find_device(const char* name) noexcept;
    [[nodiscard]] static BlockDevice* find_device_by_id(uint32_t id) noexcept;

    [[nodiscard]] static BlockDevice* default_boot_device() noexcept;
    static void set_default_boot_device(BlockDevice* dev) noexcept;

    static void dump_devices();

private:
    static BlockDevice* s_devices[MAX_BLOCK_DEVICES];
    static size_t       s_device_count;
    static BlockDevice* s_boot_device;
    static bool         s_initialized;
};

} // namespace llamaos::storage
