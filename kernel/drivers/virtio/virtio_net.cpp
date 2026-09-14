#include "virtio_net.hpp"
#include "arch/x86_64/cpu/io.hpp"
#include "memory/pmm.hpp"
#include "memory/memory_layout.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - VirtIO Network Device Driver Implementation
// =============================================================================

namespace llamaos::drivers::virtio {

using namespace arch::x86_64;

VirtioNetDevice VirtioNetDevice::s_devices[MAX_NET_DEVICES]{};
size_t          VirtioNetDevice::s_device_count = 0;

VirtioNetDevice::~VirtioNetDevice() {
    m_rx_queue.cleanup();
    m_tx_queue.cleanup();
}

size_t VirtioNetDevice::probe_all() {
    if (s_device_count > 0) {
        return s_device_count;
    }

    size_t pci_count = PciManager::device_count();
    const PciDevice* devices = PciManager::devices();

    for (size_t i = 0; i < pci_count && s_device_count < MAX_NET_DEVICES; ++i) {
        const PciDevice& dev = devices[i];
        if (dev.vendor_id == VIRTIO_PCI_VENDOR_ID &&
            (dev.device_id == VIRTIO_PCI_DEV_NET_LEGACY || dev.device_id == VIRTIO_PCI_DEV_NET_MODERN)) {
            klog_info("VirtIO-Net: Found network adapter at PCI %02x:%02x.%u (Device ID: %04x)",
                      dev.bus, dev.device, dev.function, dev.device_id);

            if (s_devices[s_device_count].init(dev, static_cast<uint32_t>(s_device_count))) {
                klog_info("VirtIO-Net: Initialized interface net%u", static_cast<uint32_t>(s_device_count));
                s_device_count++;
            }
        }
    }

    klog_info("VirtIO-Net: Discovered %u network interface(s)", static_cast<uint32_t>(s_device_count));
    return s_device_count;
}

bool VirtioNetDevice::init(const PciDevice& pci_dev, uint32_t if_index) {
    m_pci = pci_dev;
    m_if_index = if_index;

    // Enable bus mastering and I/O space
    PciManager::enable_bus_mastering(m_pci);
    PciManager::enable_io_space(m_pci);

    // Locate I/O BAR
    for (size_t b = 0; b < 6; ++b) {
        if (m_pci.bars[b].valid && m_pci.bars[b].is_io) {
            m_io_base = static_cast<uint16_t>(m_pci.bars[b].base_address);
            break;
        }
    }

    if (m_io_base == 0) {
        klog_error("VirtIO-Net: Failed to locate valid I/O BAR!");
        return false;
    }

    // 1. Reset device
    outb(m_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_RESET);

    // 2. Set ACKNOWLEDGE and DRIVER status bits
    outb(m_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    outb(m_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // 3. Negotiate features
    uint32_t host_features = inl(m_io_base + VIRTIO_PCI_HOST_FEATURES);
    uint32_t guest_features = 0;
    if (host_features & VIRTIO_NET_F_MAC) {
        guest_features |= VIRTIO_NET_F_MAC;
    }
    if (host_features & VIRTIO_NET_F_STATUS) {
        guest_features |= VIRTIO_NET_F_STATUS;
    }
    outl(m_io_base + VIRTIO_PCI_GUEST_FEATURES, guest_features);

    // 4. Read MAC address from device configuration space (offset 0x14)
    for (size_t i = 0; i < 6; ++i) {
        m_mac[i] = inb(m_io_base + VIRTIO_PCI_CONFIG_BASE + i);
    }

    // 5. Initialize Virtqueues: RX (queue 0) and TX (queue 1)
    outw(m_io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    uint16_t rx_size = inw(m_io_base + VIRTIO_PCI_QUEUE_SIZE);

    outw(m_io_base + VIRTIO_PCI_QUEUE_SEL, 1);
    uint16_t tx_size = inw(m_io_base + VIRTIO_PCI_QUEUE_SIZE);

    if (rx_size == 0 || tx_size == 0) {
        klog_error("VirtIO-Net: Invalid virtqueue sizes (RX: %u, TX: %u)!", rx_size, tx_size);
        return false;
    }

    if (!m_rx_queue.init(0, rx_size, m_io_base) || !m_tx_queue.init(1, tx_size, m_io_base)) {
        klog_error("VirtIO-Net: Failed to initialize split virtqueues!");
        return false;
    }

    // 6. Allocate memory pools for RX and TX buffers
    // RX buffers: RX_BUFFER_COUNT * 2048 bytes (8 pages of 4096 bytes)
    size_t rx_pages = (RX_BUFFER_COUNT * BUFFER_SIZE + 4095) / 4096;
    m_rx_buffers_pa = memory::g_pmm.alloc_pages(memory::PageCount(rx_pages));
    if (m_rx_buffers_pa.is_null()) {
        klog_error("VirtIO-Net: Out of physical memory allocating RX buffers!");
        return false;
    }
    m_rx_buffers = reinterpret_cast<uint8_t*>(memory::phys_to_virt(m_rx_buffers_pa).as_ptr());
    llamaos::memset(m_rx_buffers, 0, rx_pages * 4096);

    // TX buffer: 1 page (4096 bytes)
    m_tx_buffer_pa = memory::g_pmm.alloc_page();
    if (m_tx_buffer_pa.is_null()) {
        klog_error("VirtIO-Net: Out of physical memory allocating TX buffer!");
        return false;
    }
    m_tx_buffer = reinterpret_cast<uint8_t*>(memory::phys_to_virt(m_tx_buffer_pa).as_ptr());
    llamaos::memset(m_tx_buffer, 0, 4096);

    // 7. Populate RX virtqueue with receive buffers
    for (size_t i = 0; i < RX_BUFFER_COUNT; ++i) {
        int16_t desc = m_rx_queue.alloc_descriptor();
        if (desc < 0) {
            klog_error("VirtIO-Net: Failed to allocate RX descriptor %u", static_cast<uint32_t>(i));
            break;
        }

        uint64_t buf_pa = m_rx_buffers_pa.value() + (i * BUFFER_SIZE);
        m_rx_queue.set_descriptor(static_cast<uint16_t>(desc), buf_pa, BUFFER_SIZE, VIRTQ_DESC_F_WRITE, 0);
        m_rx_queue.submit_descriptor_chain(static_cast<uint16_t>(desc));
    }

    // 8. Set DRIVER_OK status
    outb(m_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    // Notify hypervisor of available RX buffers now that DRIVER_OK status is asserted
    outw(m_io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    m_initialized = true;
    dump_info();
    return true;
}

bool VirtioNetDevice::transmit(const void* frame, size_t length) {
    if (!m_initialized || !frame || length == 0 || length > (BUFFER_SIZE - sizeof(VirtioNetHdr))) {
        return false;
    }

    sync::SpinlockGuard guard(m_tx_lock);

    // Fill VirtioNetHdr at beginning of TX buffer
    VirtioNetHdr* hdr = reinterpret_cast<VirtioNetHdr*>(m_tx_buffer);
    llamaos::memset(hdr, 0, sizeof(VirtioNetHdr));

    // Copy Ethernet frame data immediately after header
    llamaos::memcpy(m_tx_buffer + sizeof(VirtioNetHdr), frame, length);

    uint32_t total_len = static_cast<uint32_t>(sizeof(VirtioNetHdr) + length);

    int16_t desc = m_tx_queue.alloc_descriptor();
    if (desc < 0) {
        klog_error("VirtIO-Net: TX queue out of descriptors!");
        return false;
    }

    uint16_t desc_idx = static_cast<uint16_t>(desc);
    m_tx_queue.set_descriptor(desc_idx, m_tx_buffer_pa.value(), total_len, 0, 0);
    m_tx_queue.submit_descriptor_chain(desc_idx);

    bool ok = m_tx_queue.wait_for_completion(desc_idx, 1000000);
    m_tx_queue.free_descriptor_chain(desc_idx);

    return ok;
}

size_t VirtioNetDevice::poll_rx(void* out_buffer, size_t max_length) {
    if (!m_initialized || !out_buffer || max_length == 0) {
        return 0;
    }

    sync::SpinlockGuard guard(m_rx_lock);

    uint16_t head = 0;
    uint32_t total_len = 0;

    if (!m_rx_queue.pop_used_buffer(&head, &total_len)) {
        return 0; // No packets pending
    }

    size_t frame_len = 0;
    if (total_len > sizeof(VirtioNetHdr)) {
        frame_len = total_len - sizeof(VirtioNetHdr);
        if (frame_len > max_length) {
            frame_len = max_length;
        }

        // Locate corresponding RX buffer
        uint32_t buf_idx = head % RX_BUFFER_COUNT;
        uint8_t* buf_ptr = m_rx_buffers + (buf_idx * BUFFER_SIZE);

        llamaos::memcpy(out_buffer, buf_ptr + sizeof(VirtioNetHdr), frame_len);
    }

    // Re-queue the descriptor back into RX ring
    uint32_t buf_idx = head % RX_BUFFER_COUNT;
    uint64_t buf_pa = m_rx_buffers_pa.value() + (buf_idx * BUFFER_SIZE);
    m_rx_queue.set_descriptor(head, buf_pa, BUFFER_SIZE, VIRTQ_DESC_F_WRITE, 0);
    m_rx_queue.submit_descriptor_chain(head);

    return frame_len;
}

void VirtioNetDevice::dump_info() const {
    klog_info("VirtIO-Net Interface Info:");
    klog_info("  I/O Base Port : 0x%04x", m_io_base);
    klog_info("  MAC Address   : %02x:%02x:%02x:%02x:%02x:%02x",
              m_mac[0], m_mac[1], m_mac[2], m_mac[3], m_mac[4], m_mac[5]);
    klog_info("  Virtqueues    : RX Size=%u, TX Size=%u, RX Buffers=%u",
              m_rx_queue.queue_size(), m_tx_queue.queue_size(), static_cast<uint32_t>(RX_BUFFER_COUNT));
}

} // namespace llamaos::drivers::virtio
