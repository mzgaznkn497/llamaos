#pragma once

#include "core/types.hpp"
#include "storage/block_device.hpp"
#include "drivers/pci/pci.hpp"
#include "virtio_queue.hpp"
#include "virtio_defs.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - VirtIO Block Device Driver
// =============================================================================
// Implements OASIS VirtIO-Block over PCI transport.
// Handles PCI discovery, capability negotiation, virtqueue submission,
// sector reads, sector writes, flush commands, and status checking.
// =============================================================================

namespace llamaos::drivers::virtio {

class VirtioBlockDevice : public storage::BlockDevice {
public:
    static constexpr size_t MAX_VIRTIO_DISKS = 4;

    VirtioBlockDevice() noexcept = default;
    VirtioBlockDevice(const PciDevice& pci_dev, uint32_t disk_index) noexcept;
    ~VirtioBlockDevice() override;

    // Discovers all VirtIO block devices on PCI bus and registers them with StorageManager
    static size_t probe_all();

    // Hardware initialization
    bool init();

    // BlockDevice interface implementation
    [[nodiscard]] const char* name() const noexcept override { return m_name; }
    [[nodiscard]] uint32_t device_id() const noexcept override { return m_device_id; }
    [[nodiscard]] uint32_t sector_size() const noexcept override { return 512; }
    [[nodiscard]] uint64_t total_sectors() const noexcept override { return m_total_sectors; }
    [[nodiscard]] bool is_read_only() const noexcept override { return m_read_only; }

    storage::BlockStatus read_sectors(uint64_t lba, uint32_t count, void* dst) override;
    storage::BlockStatus write_sectors(uint64_t lba, uint32_t count, const void* src) override;
    storage::BlockStatus flush() override;

    // Diagnostics
    void dump_info() const;

private:
    PciDevice       m_pci;
    uint32_t        m_disk_index{0};
    uint32_t        m_device_id{0};
    uint16_t        m_io_base{0};
    uint64_t        m_total_sectors{0};
    bool            m_read_only{false};
    bool            m_supports_flush{false};
    bool            m_initialized{false};
    char            m_name[NAME_MAX_LEN]{0};

    Virtqueue       m_requestq;
    sync::Spinlock  m_io_lock;

    // Physical DMA bounce buffer for safe DMA transfers across arbitrary address spaces
    static constexpr size_t DMA_BOUNCE_SECTORS = 64; // 32 KiB
    static constexpr size_t DMA_BOUNCE_BYTES   = DMA_BOUNCE_SECTORS * 512;
    static constexpr size_t DMA_PAGE_COUNT     = 1 + (DMA_BOUNCE_BYTES / 4096); // 9 pages (36 KiB)

    memory::PhysicalAddress m_dma_phys{0};
    uint8_t*                m_dma_virt{nullptr};

    // Static pool of driver instances
    static VirtioBlockDevice s_instances[MAX_VIRTIO_DISKS];
    static size_t            s_instance_count;
};

} // namespace llamaos::drivers::virtio
