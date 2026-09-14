#pragma once

#include "net_types.hpp"
#include "arp.hpp"
#include "drivers/virtio/virtio_net.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - Unified Network Interface Layer
// =============================================================================

namespace llamaos::net {

class NetInterface {
public:
    static constexpr size_t MAX_FRAME_SIZE = 1518;
    static constexpr size_t MAX_INTERFACES = 4;

    NetInterface() : m_arp(this) {}

    bool init(drivers::virtio::VirtioNetDevice* dev,
              const char* name = "net0",
              Ipv4Address ip = Ipv4Address::any(),
              Ipv4Address netmask = Ipv4Address::any(),
              Ipv4Address gateway = Ipv4Address::any());

    // Poll for received frames and process network queue
    void poll();

    // Transmit operations
    bool send_ethernet(const MacAddress& dest_mac, uint16_t ethertype, const void* payload, size_t len);
    bool send_ipv4(Ipv4Address dst_ip, uint8_t protocol, const void* payload, size_t len);

    // IP address configuration
    void set_ip(Ipv4Address ip) { m_ip = ip; }
    void set_netmask(Ipv4Address mask) { m_netmask = mask; }
    void set_gateway(Ipv4Address gw);
    void set_dns(Ipv4Address dns) { m_dns = dns; }

    [[nodiscard]] const char* name() const noexcept { return m_name; }
    [[nodiscard]] Ipv4Address ip() const noexcept { return m_ip; }
    [[nodiscard]] Ipv4Address netmask() const noexcept { return m_netmask; }
    [[nodiscard]] Ipv4Address gateway() const noexcept { return m_gateway; }
    [[nodiscard]] Ipv4Address dns() const noexcept { return m_dns; }
    [[nodiscard]] const MacAddress& mac() const noexcept { return m_mac; }
    [[nodiscard]] ArpEngine& arp() noexcept { return m_arp; }
    [[nodiscard]] bool is_up() const noexcept { return m_up; }

    // Multi-NIC Interface Registry
    static NetInterface* register_device(drivers::virtio::VirtioNetDevice* dev, const char* name = nullptr);
    static NetInterface* default_interface() noexcept;
    static NetInterface* get_interface(size_t index) noexcept;
    static NetInterface* find_by_name(const char* name) noexcept;
    static size_t interface_count() noexcept { return s_interface_count; }
    static void poll_all();

private:
    void handle_ipv4(const uint8_t* payload, size_t len);

    drivers::virtio::VirtioNetDevice* m_dev{nullptr};
    char                              m_name[16]{"net0"};
    MacAddress                        m_mac{};
    Ipv4Address                       m_ip{};
    Ipv4Address                       m_netmask{};
    Ipv4Address                       m_gateway{};
    Ipv4Address                       m_dns{};
    ArpEngine                         m_arp;
    bool                              m_up{false};
    uint16_t                          m_next_ip_id{1};
    sync::Spinlock                    m_tx_lock;

    static NetInterface s_interfaces[MAX_INTERFACES];
    static size_t       s_interface_count;
};

} // namespace llamaos::net
