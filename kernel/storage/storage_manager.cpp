#include "storage/storage_manager.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"
#include "sync/spinlock.hpp"

namespace llamaos::storage {

BlockDevice* StorageManager::s_devices[MAX_BLOCK_DEVICES]{};
size_t       StorageManager::s_device_count{0};
BlockDevice* StorageManager::s_boot_device{nullptr};
bool         StorageManager::s_initialized{false};

static sync::Spinlock s_storage_lock;

void StorageManager::init() {
    sync::SpinlockGuard guard(s_storage_lock);
    for (size_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        s_devices[i] = nullptr;
    }
    s_device_count = 0;
    s_boot_device = nullptr;
    s_initialized = true;

    klog_info("StorageManager initialized (Max Block Devices: %u).",
              static_cast<uint32_t>(MAX_BLOCK_DEVICES));
}

bool StorageManager::register_device(BlockDevice* dev) {
    if (!dev) return false;

    sync::SpinlockGuard guard(s_storage_lock);
    if (!s_initialized) {
        // Safe auto-init if called early
        for (size_t i = 0; i < MAX_BLOCK_DEVICES; ++i) s_devices[i] = nullptr;
        s_device_count = 0;
        s_initialized = true;
    }

    if (s_device_count >= MAX_BLOCK_DEVICES) {
        klog_error("StorageManager: device table full, cannot register '%s'!", dev->name());
        return false;
    }

    // Check duplicate name
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] && llamaos::strcmp(s_devices[i]->name(), dev->name()) == 0) {
            klog_warn("StorageManager: device '%s' already registered!", dev->name());
            return false;
        }
    }

    s_devices[s_device_count++] = dev;

    if (!s_boot_device) {
        s_boot_device = dev;
    }

    klog_info("StorageManager: Registered block device '%s' (ID %u, %llu MiB, %llu sectors, %u B/sector%s)",
              dev->name(),
              dev->device_id(),
              dev->capacity_bytes() / (1024 * 1024),
              dev->total_sectors(),
              dev->sector_size(),
              dev->is_read_only() ? ", READ-ONLY" : "");

    return true;
}

bool StorageManager::unregister_device(BlockDevice* dev) {
    if (!dev) return false;

    sync::SpinlockGuard guard(s_storage_lock);
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] == dev) {
            if (s_boot_device == dev) {
                s_boot_device = nullptr;
            }
            // Shift remaining devices
            for (size_t j = i; j + 1 < s_device_count; ++j) {
                s_devices[j] = s_devices[j + 1];
            }
            s_devices[--s_device_count] = nullptr;
            klog_info("StorageManager: Unregistered block device '%s'", dev->name());
            return true;
        }
    }
    return false;
}

size_t StorageManager::device_count() noexcept {
    return s_device_count;
}

BlockDevice* StorageManager::get_device(size_t index) noexcept {
    if (index >= s_device_count) return nullptr;
    return s_devices[index];
}

BlockDevice* StorageManager::find_device(const char* name) noexcept {
    if (!name) return nullptr;
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] && llamaos::strcmp(s_devices[i]->name(), name) == 0) {
            return s_devices[i];
        }
    }
    return nullptr;
}

BlockDevice* StorageManager::find_device_by_id(uint32_t id) noexcept {
    for (size_t i = 0; i < s_device_count; ++i) {
        if (s_devices[i] && s_devices[i]->device_id() == id) {
            return s_devices[i];
        }
    }
    return nullptr;
}

BlockDevice* StorageManager::default_boot_device() noexcept {
    return s_boot_device;
}

void StorageManager::set_default_boot_device(BlockDevice* dev) noexcept {
    s_boot_device = dev;
}

void StorageManager::dump_devices() {
    klog_info("=== Block Storage Devices (%u detected) ===", static_cast<uint32_t>(s_device_count));
    for (size_t i = 0; i < s_device_count; ++i) {
        BlockDevice* d = s_devices[i];
        if (!d) continue;
        klog_info("  [%02u] '%s' (ID: %u) - %llu MiB (%llu sectors @ %u B)%s",
                  static_cast<uint32_t>(i),
                  d->name(),
                  d->device_id(),
                  d->capacity_bytes() / (1024 * 1024),
                  d->total_sectors(),
                  d->sector_size(),
                  d == s_boot_device ? " [BOOT]" : "");
    }
}

} // namespace llamaos::storage
