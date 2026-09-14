#pragma once

#include "core/types.hpp"
#include "drivers/pci/pci.hpp"
#include "virtio_queue.hpp"
#include "virtio_defs.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - VirtIO Network Device Driver (Multi-NIC Support)
// =============================================================================
// Implements OASIS VirtIO-Net 0.9.5/1.0 over PCI transport.
// Handles PCI discovery (1AF4:1000 and 1AF4:1041), multi-NIC enumeration,
// MAC acquisition, split virtqueue setup for RX (queue 0) and TX (queue 1),
// packet transmission, and polling reception.
// =============================================================================

namespace llamaos::drivers::virtio {

class VirtioNetDevice {
public:
    static constexpr size_t MAX_NET_DEVICES = 4;
    static constexpr size_t RX_BUFFER_COUNT = 16;
    static constexpr size_t BUFFER_SIZE     = 2048; // Must accommodate VirtioNetHdr + MTU 1514

    VirtioNetDevice() noexcept = default;
    ~VirtioNetDevice();

    // Discovers all VirtIO network devices on PCI bus and initializes them
    static size_t probe_all();
    static VirtioNetDevice* probe() {
        if (s_device_count == 0) probe_all();
        return default_device();
    }
    static VirtioNetDevice* default_device() noexcept {
        return s_device_count > 0 ? &s_devices[0] : nullptr;
    }
    static VirtioNetDevice* get_device(size_t index) noexcept {
        return (index < s_device_count) ? &s_devices[index] : nullptr;
    }
    static size_t device_count() noexcept { return s_device_count; }

    bool init(const PciDevice& pci_dev, uint32_t if_index = 0);

    // Transmit raw Ethernet frame
    bool transmit(const void* frame, size_t length);

    // Poll for received Ethernet frame. Returns length of frame (0 if no packet)
    size_t poll_rx(void* out_buffer, size_t max_length);

    // Device properties
    [[nodiscard]] const uint8_t* mac_address() const noexcept { return m_mac; }
    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }
    [[nodiscard]] uint16_t io_base() const noexcept { return m_io_base; }
    [[nodiscard]] uint32_t interface_index() const noexcept { return m_if_index; }

    void dump_info() const;

private:
    PciDevice       m_pci{};
    uint16_t        m_io_base{0};
    uint32_t        m_if_index{0};
    uint8_t         m_mac[6]{0};
    bool            m_initialized{false};

    Virtqueue       m_rx_queue;
    Virtqueue       m_tx_queue;
    sync::Spinlock  m_tx_lock;
    sync::Spinlock  m_rx_lock;

    // Buffer pools
    uint8_t*        m_rx_buffers{nullptr};
    memory::PhysicalAddress m_rx_buffers_pa{0};

    uint8_t*        m_tx_buffer{nullptr};
    memory::PhysicalAddress m_tx_buffer_pa{0};

    static VirtioNetDevice s_devices[MAX_NET_DEVICES];
    static size_t          s_device_count;
};

} // namespace llamaos::drivers::virtio
