#include "net_interface.hpp"
#include "icmp.hpp"
#include "udp.hpp"
#include "tcp.hpp"
#include "dhcp.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Unified Network Interface Layer Implementation
// =============================================================================

namespace llamaos::net {

NetInterface NetInterface::s_interfaces[MAX_INTERFACES]{};
size_t       NetInterface::s_interface_count = 0;

NetInterface* NetInterface::register_device(drivers::virtio::VirtioNetDevice* dev, const char* name) {
    if (!dev || s_interface_count >= MAX_INTERFACES) return nullptr;
    NetInterface* iface = &s_interfaces[s_interface_count];
    char if_name[16];
    if (name) {
        llamaos::strncpy(if_name, name, sizeof(if_name) - 1);
        if_name[sizeof(if_name) - 1] = '\0';
    } else {
        if_name[0] = 'n';
        if_name[1] = 'e';
        if_name[2] = 't';
        if_name[3] = static_cast<char>('0' + s_interface_count);
        if_name[4] = '\0';
    }

    if (iface->init(dev, if_name)) {
        s_interface_count++;
        return iface;
    }
    return nullptr;
}

NetInterface* NetInterface::default_interface() noexcept {
    return s_interface_count > 0 ? &s_interfaces[0] : nullptr;
}

NetInterface* NetInterface::get_interface(size_t index) noexcept {
    return (index < s_interface_count) ? &s_interfaces[index] : nullptr;
}

NetInterface* NetInterface::find_by_name(const char* name) noexcept {
    if (!name) return nullptr;
    for (size_t i = 0; i < s_interface_count; ++i) {
        if (llamaos::strcmp(s_interfaces[i].name(), name) == 0) {
            return &s_interfaces[i];
        }
    }
    return nullptr;
}

void NetInterface::poll_all() {
    for (size_t i = 0; i < s_interface_count; ++i) {
        s_interfaces[i].poll();
    }
}

void NetInterface::set_gateway(Ipv4Address gw) {
    m_gateway = gw;
    if (m_gateway.raw != 0 && m_up) {
        m_arp.send_request(m_gateway);
    }
}

bool NetInterface::init(drivers::virtio::VirtioNetDevice* dev,
                        const char* name,
                        Ipv4Address ip,
                        Ipv4Address netmask,
                        Ipv4Address gateway) {
    if (!dev || !dev->is_initialized()) {
        return false;
    }

    m_dev = dev;
    if (name) {
        llamaos::strncpy(m_name, name, sizeof(m_name) - 1);
        m_name[sizeof(m_name) - 1] = '\0';
    }
    const uint8_t* m = dev->mac_address();
    m_mac = MacAddress(m[0], m[1], m[2], m[3], m[4], m[5]);

    m_ip = ip;
    m_netmask = netmask;
    m_gateway = gateway;
    m_dns = Ipv4Address::any();

    m_arp.init();
    TcpEngine::init();
    DhcpClient::init(this);

    // Dynamic ARP: Do NOT pre-populate fake QEMU gateway MAC!
    // Gateway MAC will be resolved dynamically over standard ARP exchange.

    m_up = true;

    // Send ARP query to gateway if configured so router learns guest MAC immediately
    if (m_gateway.raw != 0) {
        m_arp.send_request(m_gateway);
    }

    klog_info("================================================================================");
    klog_info(" [PASS] LlamaOS/A Network Interface '%s' Online:", m_name);
    klog_info("   Hardware MAC : %02x:%02x:%02x:%02x:%02x:%02x",
              m_mac.bytes[0], m_mac.bytes[1], m_mac.bytes[2],
              m_mac.bytes[3], m_mac.bytes[4], m_mac.bytes[5]);
    if (m_ip.raw != 0) {
        klog_info("   Config IPv4  : %u.%u.%u.%u / %u.%u.%u.%u",
                  m_ip.octet(0), m_ip.octet(1), m_ip.octet(2), m_ip.octet(3),
                  m_netmask.octet(0), m_netmask.octet(1), m_netmask.octet(2), m_netmask.octet(3));
        klog_info("   Default GW   : %u.%u.%u.%u (Dynamic ARP resolution active)",
                  m_gateway.octet(0), m_gateway.octet(1), m_gateway.octet(2), m_gateway.octet(3));
    } else {
        klog_info("   IPv4 State   : Unconfigured (0.0.0.0) -> Awaiting DHCP or static config");
    }
    klog_info("================================================================================");

    return true;
}

bool NetInterface::send_ethernet(const MacAddress& dest_mac, uint16_t ethertype, const void* payload, size_t len) {
    if (!m_dev || !m_up || len > (MAX_FRAME_SIZE - sizeof(EthernetHeader))) {
        return false;
    }

    sync::SpinlockGuard guard(m_tx_lock);

    alignas(4) uint8_t frame[MAX_FRAME_SIZE];
    EthernetHeader* eth = reinterpret_cast<EthernetHeader*>(frame);

    eth->dest = dest_mac;
    eth->src  = m_mac;
    eth->ethertype = htons(ethertype);

    if (payload && len > 0) {
        llamaos::memcpy(frame + sizeof(EthernetHeader), payload, len);
    }

    size_t total_len = sizeof(EthernetHeader) + len;
    // Minimum Ethernet frame length is 60 bytes (excluding 4-byte FCS)
    if (total_len < 60) {
        llamaos::memset(frame + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    return m_dev->transmit(frame, total_len);
}

bool NetInterface::send_ipv4(Ipv4Address dst_ip, uint8_t protocol, const void* payload, size_t len) {
    if (!m_dev || !m_up || len > 1460) return false;

    // Routing determination
    MacAddress target_mac;
    if (dst_ip == Ipv4Address::broadcast() || dst_ip == Ipv4Address(255, 255, 255, 255)) {
        target_mac = MacAddress::broadcast();
    } else {
        // Determine whether target is in local subnet
        bool same_subnet = (dst_ip.raw & m_netmask.raw) == (m_ip.raw & m_netmask.raw);
        Ipv4Address next_hop = same_subnet ? dst_ip : m_gateway;

        if (!m_arp.lookup(next_hop, target_mac)) {
            // Send ARP query and fallback to broadcast
            m_arp.send_request(next_hop);
            target_mac = MacAddress::broadcast();
        }
    }

    alignas(4) uint8_t ip_packet[1500];
    Ipv4Header* ip = reinterpret_cast<Ipv4Header*>(ip_packet);

    ip->ver_ihl      = 0x45; // Version 4, 5 * 4 = 20 bytes
    ip->dscp_ecn     = 0;
    ip->total_length = htons(static_cast<uint16_t>(sizeof(Ipv4Header) + len));
    ip->id           = htons(m_next_ip_id++);
    ip->flags_frag   = htons(0x4000); // DF bit set
    ip->ttl          = 64;
    ip->protocol     = protocol;
    ip->checksum     = 0;
    ip->src_ip       = m_ip;
    ip->dst_ip       = dst_ip;

    ip->checksum = calculate_checksum(ip, sizeof(Ipv4Header));

    if (payload && len > 0) {
        llamaos::memcpy(ip_packet + sizeof(Ipv4Header), payload, len);
    }

    return send_ethernet(target_mac, ETHERTYPE_IPV4, ip_packet, sizeof(Ipv4Header) + len);
}

void NetInterface::poll() {
    if (!m_dev || !m_up) return;

    alignas(4) uint8_t frame[MAX_FRAME_SIZE];
    size_t frame_len = 0;

    // Drain pending packets from VirtIO RX queue
    while ((frame_len = m_dev->poll_rx(frame, sizeof(frame))) > 0) {
        if (frame_len < sizeof(EthernetHeader)) continue;

        const EthernetHeader* eth = reinterpret_cast<const EthernetHeader*>(frame);

        // Filter packets: must be for our MAC, broadcast, or multicast
        if (eth->dest != m_mac && !eth->dest.is_broadcast()) {
            continue;
        }

        uint16_t ethertype = ntohs(eth->ethertype);
        const uint8_t* payload = frame + sizeof(EthernetHeader);
        size_t payload_len = frame_len - sizeof(EthernetHeader);

        if (ethertype == ETHERTYPE_ARP) {
            m_arp.handle_packet(payload, payload_len);
        } else if (ethertype == ETHERTYPE_IPV4) {
            if (payload_len >= sizeof(Ipv4Header)) {
                const auto* ip = reinterpret_cast<const Ipv4Header*>(payload);
                m_arp.insert(ip->src_ip, eth->src);
            }
            handle_ipv4(payload, payload_len);
        }
    }
}

void NetInterface::handle_ipv4(const uint8_t* payload, size_t len) {
    if (!payload || len < sizeof(Ipv4Header)) return;

    const Ipv4Header* ip = reinterpret_cast<const Ipv4Header*>(payload);

    if (ip->version() != 4) return;
    uint8_t hdr_len = ip->header_len();
    if (hdr_len < sizeof(Ipv4Header) || hdr_len > len) return;

    uint16_t total_len = ntohs(ip->total_length);
    if (total_len > len) return;

    // Checksum verification
    if (calculate_checksum(ip, hdr_len) != 0) {
        klog_warn("IPv4: Bad checksum on incoming packet!");
        return;
    }

    // Filter packet destination: allow our IP, broadcast, or unconfigured (for DHCP)
    bool dst_ok = (ip->dst_ip == m_ip) ||
                  (ip->dst_ip == Ipv4Address::broadcast()) ||
                  (ip->dst_ip == Ipv4Address(255, 255, 255, 255)) ||
                  (m_ip == Ipv4Address::any());
    if (!dst_ok) {
        return;
    }

    const uint8_t* proto_payload = payload + hdr_len;
    size_t proto_len = total_len - hdr_len;

    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            IcmpEngine::handle_packet(this, ip->src_ip, proto_payload, proto_len);
            break;

        case IP_PROTO_UDP:
            UdpEngine::handle_packet(this, ip->src_ip, proto_payload, proto_len);
            break;

        case IP_PROTO_TCP:
            TcpEngine::handle_packet(this, ip->src_ip, proto_payload, proto_len);
            break;

        default:
            break;
    }
}

} // namespace llamaos::net
