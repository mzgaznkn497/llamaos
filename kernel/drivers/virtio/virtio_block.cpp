#include "virtio_block.hpp"
#include "storage/storage_manager.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "memory/memory_types.hpp"
#include "memory/pmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

namespace llamaos::drivers::virtio {

VirtioBlockDevice VirtioBlockDevice::s_instances[MAX_VIRTIO_DISKS]{};
size_t VirtioBlockDevice::s_instance_count{0};

VirtioBlockDevice::VirtioBlockDevice(const PciDevice& pci_dev, uint32_t disk_index) noexcept
    : m_pci(pci_dev),
      m_disk_index(disk_index),
      m_device_id(100 + disk_index) {
    m_name[0] = 'v';
    m_name[1] = 'd';
    m_name[2] = static_cast<char>('a' + disk_index);
    m_name[3] = '\0';
}

VirtioBlockDevice::~VirtioBlockDevice() {
    if (m_initialized && m_io_base) {
        arch::x86_64::outb(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_RESET);
    }
    if (!m_dma_phys.is_null()) {
        memory::g_pmm.free_pages(m_dma_phys, memory::PageCount(DMA_PAGE_COUNT));
        m_dma_phys = memory::PhysicalAddress(0);
        m_dma_virt = nullptr;
    }
}

size_t VirtioBlockDevice::probe_all() {
    size_t discovered = 0;
    size_t pci_count = PciManager::device_count();
    const PciDevice* devices = PciManager::devices();

    for (size_t i = 0; i < pci_count && s_instance_count < MAX_VIRTIO_DISKS; ++i) {
        const PciDevice& dev = devices[i];
        if (dev.vendor_id == VIRTIO_PCI_VENDOR_ID &&
            (dev.device_id == VIRTIO_PCI_DEV_BLK_LEGACY || dev.device_id == VIRTIO_PCI_DEV_BLK_MODERN)) {
            
            VirtioBlockDevice& blk = s_instances[s_instance_count];
            blk.m_pci = dev;
            blk.m_disk_index = static_cast<uint32_t>(s_instance_count);
            blk.m_device_id = 100 + static_cast<uint32_t>(s_instance_count);
            blk.m_name[0] = 'v';
            blk.m_name[1] = 'd';
            blk.m_name[2] = static_cast<char>('a' + s_instance_count);
            blk.m_name[3] = '\0';

            if (blk.init()) {
                storage::StorageManager::register_device(&blk);
                s_instance_count++;
                discovered++;
            }
        }
    }

    return discovered;
}

bool VirtioBlockDevice::init() {
    // 1. Locate I/O BAR
    bool found_io = false;
    for (size_t b = 0; b < 6; ++b) {
        if (m_pci.bars[b].valid && m_pci.bars[b].is_io) {
            m_io_base = static_cast<uint16_t>(m_pci.bars[b].base_address);
            found_io = true;
            break;
        }
    }

    if (!found_io || m_io_base == 0) {
        klog_error("VirtioBlock: Device on PCI %02x:%02x.%u has no valid I/O BAR!",
                   m_pci.bus, m_pci.device, m_pci.function);
        return false;
    }

    // 2. Enable Bus Mastering and I/O Space on PCI
    PciManager::enable_bus_mastering(m_pci);
    PciManager::enable_io_space(m_pci);

    // 3. Reset device
    arch::x86_64::outb(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_RESET);

    // 4. Set ACKNOWLEDGE and DRIVER bits
    arch::x86_64::outb(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_ACKNOWLEDGE);
    arch::x86_64::outb(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_STATUS),
                       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // 5. Read device features
    uint32_t host_features = arch::x86_64::inl(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_HOST_FEATURES));
    m_read_only = (host_features & VIRTIO_BLK_F_RO) != 0;
    m_supports_flush = (host_features & VIRTIO_BLK_F_FLUSH) != 0;

    // 6. Write guest features
    uint32_t guest_features = 0;
    if (m_read_only) guest_features |= VIRTIO_BLK_F_RO;
    if (m_supports_flush) guest_features |= VIRTIO_BLK_F_FLUSH;
    arch::x86_64::outl(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_GUEST_FEATURES), guest_features);

    // 7. Initialize request virtqueue (queue index 0)
    arch::x86_64::outw(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_QUEUE_SEL), 0);
    uint16_t qsize = arch::x86_64::inw(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_QUEUE_SIZE));
    if (qsize == 0) {
        klog_error("VirtioBlock: Device reported request queue size 0!");
        return false;
    }

    if (!m_requestq.init(0, qsize, m_io_base)) {
        klog_error("VirtioBlock: Failed to allocate request virtqueue (size %u)!", qsize);
        return false;
    }

    // 8. Set DRIVER_OK
    arch::x86_64::outb(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_STATUS),
                        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    // 9. Allocate contiguous physical DMA pages for safe virtqueue transfers
    m_dma_phys = memory::g_pmm.alloc_pages(memory::PageCount(DMA_PAGE_COUNT));
    if (m_dma_phys.is_null()) {
        klog_error("VirtioBlock: Failed to allocate physical DMA pages!");
        return false;
    }
    m_dma_virt = reinterpret_cast<uint8_t*>(memory::phys_to_virt(m_dma_phys).as_ptr());
    memset(m_dma_virt, 0, DMA_PAGE_COUNT * 4096);

    // 10. Read disk capacity in 512-byte sectors from device configuration space
    uint32_t cap_low = arch::x86_64::inl(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_CONFIG_BASE));
    uint32_t cap_high = arch::x86_64::inl(static_cast<uint16_t>(m_io_base + VIRTIO_PCI_CONFIG_BASE + 4));
    m_total_sectors = (static_cast<uint64_t>(cap_high) << 32) | cap_low;

    m_initialized = true;

    klog_info("VirtioBlock: Initialized '%s' at I/O 0x%04x: %llu sectors (%llu MiB)%s",
              m_name,
              m_io_base,
              m_total_sectors,
              capacity_bytes() / (1024 * 1024),
              m_read_only ? " [RO]" : "");

    return true;
}

storage::BlockStatus VirtioBlockDevice::read_sectors(uint64_t lba, uint32_t count, void* dst) {
    if (!m_initialized || !m_dma_virt) return storage::BlockStatus::DeviceFault;
    if (count == 0) return storage::BlockStatus::Success;
    if (!validate_bounds(lba, count, dst, false)) return storage::BlockStatus::OutOfBounds;

    sync::IrqSpinlockGuard guard(m_io_lock);

    auto* req_hdr = reinterpret_cast<VirtioBlockReqHeader*>(m_dma_virt);
    auto* status_byte = reinterpret_cast<volatile uint8_t*>(m_dma_virt + 64);
    uint8_t* bounce_buf = m_dma_virt + 4096;

    uint64_t hdr_paddr = m_dma_phys.value();
    uint64_t status_paddr = m_dma_phys.value() + 64;
    uint64_t bounce_paddr = m_dma_phys.value() + 4096;

    uint8_t* out_ptr = static_cast<uint8_t*>(dst);
    uint64_t cur_lba = lba;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = (remaining > DMA_BOUNCE_SECTORS) ? static_cast<uint32_t>(DMA_BOUNCE_SECTORS) : remaining;
        uint32_t chunk_bytes = chunk * 512;

        req_hdr->type = VIRTIO_BLK_T_IN;
        req_hdr->ioprio = 0;
        req_hdr->sector = cur_lba;
        *status_byte = 0xFF;

        int16_t d0 = m_requestq.alloc_descriptor();
        int16_t d1 = m_requestq.alloc_descriptor();
        int16_t d2 = m_requestq.alloc_descriptor();

        if (d0 < 0 || d1 < 0 || d2 < 0) {
            if (d0 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));
            if (d1 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d1));
            if (d2 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d2));
            return storage::BlockStatus::DeviceFault;
        }

        m_requestq.set_descriptor(static_cast<uint16_t>(d0), hdr_paddr, sizeof(VirtioBlockReqHeader), VIRTQ_DESC_F_NEXT, static_cast<uint16_t>(d1));
        m_requestq.set_descriptor(static_cast<uint16_t>(d1), bounce_paddr, chunk_bytes, VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT, static_cast<uint16_t>(d2));
        m_requestq.set_descriptor(static_cast<uint16_t>(d2), status_paddr, 1, VIRTQ_DESC_F_WRITE, 0);

        m_requestq.submit_descriptor_chain(static_cast<uint16_t>(d0));

        bool ok = m_requestq.wait_for_completion(static_cast<uint16_t>(d0));
        m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));

        if (!ok) {
            return storage::BlockStatus::Timeout;
        }

        if (*status_byte != VIRTIO_BLK_S_OK) {
            if (*status_byte == VIRTIO_BLK_S_UNSUPP) return storage::BlockStatus::Unsupported;
            return storage::BlockStatus::IoError;
        }

        memcpy(out_ptr, bounce_buf, chunk_bytes);

        out_ptr += chunk_bytes;
        cur_lba += chunk;
        remaining -= chunk;
    }

    return storage::BlockStatus::Success;
}

storage::BlockStatus VirtioBlockDevice::write_sectors(uint64_t lba, uint32_t count, const void* src) {
    if (!m_initialized || !m_dma_virt) return storage::BlockStatus::DeviceFault;
    if (m_read_only) return storage::BlockStatus::ReadOnly;
    if (count == 0) return storage::BlockStatus::Success;
    if (!validate_bounds(lba, count, src, true)) return storage::BlockStatus::OutOfBounds;

    sync::IrqSpinlockGuard guard(m_io_lock);

    auto* req_hdr = reinterpret_cast<VirtioBlockReqHeader*>(m_dma_virt);
    auto* status_byte = reinterpret_cast<volatile uint8_t*>(m_dma_virt + 64);
    uint8_t* bounce_buf = m_dma_virt + 4096;

    uint64_t hdr_paddr = m_dma_phys.value();
    uint64_t status_paddr = m_dma_phys.value() + 64;
    uint64_t bounce_paddr = m_dma_phys.value() + 4096;

    const uint8_t* in_ptr = static_cast<const uint8_t*>(src);
    uint64_t cur_lba = lba;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = (remaining > DMA_BOUNCE_SECTORS) ? static_cast<uint32_t>(DMA_BOUNCE_SECTORS) : remaining;
        uint32_t chunk_bytes = chunk * 512;

        memcpy(bounce_buf, in_ptr, chunk_bytes);

        req_hdr->type = VIRTIO_BLK_T_OUT;
        req_hdr->ioprio = 0;
        req_hdr->sector = cur_lba;
        *status_byte = 0xFF;

        int16_t d0 = m_requestq.alloc_descriptor();
        int16_t d1 = m_requestq.alloc_descriptor();
        int16_t d2 = m_requestq.alloc_descriptor();

        if (d0 < 0 || d1 < 0 || d2 < 0) {
            if (d0 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));
            if (d1 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d1));
            if (d2 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d2));
            return storage::BlockStatus::DeviceFault;
        }

        m_requestq.set_descriptor(static_cast<uint16_t>(d0), hdr_paddr, sizeof(VirtioBlockReqHeader), VIRTQ_DESC_F_NEXT, static_cast<uint16_t>(d1));
        m_requestq.set_descriptor(static_cast<uint16_t>(d1), bounce_paddr, chunk_bytes, VIRTQ_DESC_F_NEXT, static_cast<uint16_t>(d2));
        m_requestq.set_descriptor(static_cast<uint16_t>(d2), status_paddr, 1, VIRTQ_DESC_F_WRITE, 0);

        m_requestq.submit_descriptor_chain(static_cast<uint16_t>(d0));

        bool ok = m_requestq.wait_for_completion(static_cast<uint16_t>(d0));
        m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));

        if (!ok) {
            return storage::BlockStatus::Timeout;
        }

        if (*status_byte != VIRTIO_BLK_S_OK) {
            if (*status_byte == VIRTIO_BLK_S_UNSUPP) return storage::BlockStatus::Unsupported;
            return storage::BlockStatus::IoError;
        }

        in_ptr += chunk_bytes;
        cur_lba += chunk;
        remaining -= chunk;
    }

    return storage::BlockStatus::Success;
}

storage::BlockStatus VirtioBlockDevice::flush() {
    if (!m_initialized || !m_dma_virt) return storage::BlockStatus::DeviceFault;
    if (!m_supports_flush) return storage::BlockStatus::Success; // Device has no volatile cache to flush

    sync::IrqSpinlockGuard guard(m_io_lock);

    auto* req_hdr = reinterpret_cast<VirtioBlockReqHeader*>(m_dma_virt);
    auto* status_byte = reinterpret_cast<volatile uint8_t*>(m_dma_virt + 64);

    uint64_t hdr_paddr = m_dma_phys.value();
    uint64_t status_paddr = m_dma_phys.value() + 64;

    req_hdr->type = VIRTIO_BLK_T_FLUSH;
    req_hdr->ioprio = 0;
    req_hdr->sector = 0;
    *status_byte = 0xFF;

    int16_t d0 = m_requestq.alloc_descriptor();
    int16_t d1 = m_requestq.alloc_descriptor();

    if (d0 < 0 || d1 < 0) {
        if (d0 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));
        if (d1 >= 0) m_requestq.free_descriptor_chain(static_cast<uint16_t>(d1));
        return storage::BlockStatus::DeviceFault;
    }

    m_requestq.set_descriptor(static_cast<uint16_t>(d0), hdr_paddr, sizeof(VirtioBlockReqHeader), VIRTQ_DESC_F_NEXT, static_cast<uint16_t>(d1));
    m_requestq.set_descriptor(static_cast<uint16_t>(d1), status_paddr, 1, VIRTQ_DESC_F_WRITE, 0);

    m_requestq.submit_descriptor_chain(static_cast<uint16_t>(d0));

    bool ok = m_requestq.wait_for_completion(static_cast<uint16_t>(d0));
    m_requestq.free_descriptor_chain(static_cast<uint16_t>(d0));

    if (!ok) {
        return storage::BlockStatus::Timeout;
    }

    return (*status_byte == VIRTIO_BLK_S_OK) ? storage::BlockStatus::Success : storage::BlockStatus::IoError;
}

void VirtioBlockDevice::dump_info() const {
    klog_info("VirtioBlock '%s': I/O Base 0x%04x, Sectors %llu (%llu MiB), RO=%b, Flush=%b",
              m_name, m_io_base, m_total_sectors, capacity_bytes() / (1024 * 1024),
              m_read_only, m_supports_flush);
}

} // namespace llamaos::drivers::virtio
