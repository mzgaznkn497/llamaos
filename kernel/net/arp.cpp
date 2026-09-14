#include "arp.hpp"
#include "net_interface.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Address Resolution Protocol (ARP) Implementation
// =============================================================================

namespace llamaos::net {

void ArpEngine::init() {
    sync::SpinlockGuard guard(m_lock);
    for (size_t i = 0; i < ARP_TABLE_CAPACITY; ++i) {
        m_table[i] = ArpEntry{};
    }
}

bool ArpEngine::lookup(Ipv4Address ip, MacAddress& out_mac) {
    sync::SpinlockGuard guard(m_lock);
    for (size_t i = 0; i < ARP_TABLE_CAPACITY; ++i) {
        if (m_table[i].valid && m_table[i].ip == ip) {
            out_mac = m_table[i].mac;
            return true;
        }
    }
    return false;
}

void ArpEngine::insert(Ipv4Address ip, const MacAddress& mac) {
    if (ip == Ipv4Address::any() || mac.is_zero() || mac.is_broadcast()) {
        return;
    }

    sync::SpinlockGuard guard(m_lock);

    // Update existing entry if present
    for (size_t i = 0; i < ARP_TABLE_CAPACITY; ++i) {
        if (m_table[i].valid && m_table[i].ip == ip) {
            m_table[i].mac = mac;
            return;
        }
    }

    // Otherwise find first free or oldest slot
    for (size_t i = 0; i < ARP_TABLE_CAPACITY; ++i) {
        if (!m_table[i].valid) {
            m_table[i].ip = ip;
            m_table[i].mac = mac;
            m_table[i].valid = true;
            return;
        }
    }

    // Overwrite slot 0 if full
    m_table[0].ip = ip;
    m_table[0].mac = mac;
    m_table[0].valid = true;
}

void ArpEngine::handle_packet(const uint8_t* payload, size_t len) {
    if (!payload || len < sizeof(ArpHeader)) return;

    const ArpHeader* arp = reinterpret_cast<const ArpHeader*>(payload);

    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET || ntohs(arp->proto_type) != ETHERTYPE_IPV4) {
        return;
    }

    uint16_t opcode = ntohs(arp->opcode);

    // Always cache the sender mapping
    insert(arp->sender_ip, arp->sender_mac);

    if (opcode == ARP_OP_REQUEST) {
        // If query is for this interface's IP address, send reply
        if (arp->target_ip == m_netif->ip()) {
            send_reply(arp->sender_ip, arp->sender_mac);
        }
    }
}

void ArpEngine::send_request(Ipv4Address target_ip) {
    ArpHeader req{};
    req.hw_type = htons(ARP_HW_ETHERNET);
    req.proto_type = htons(ETHERTYPE_IPV4);
    req.hw_len = 6;
    req.proto_len = 4;
    req.opcode = htons(ARP_OP_REQUEST);
    req.sender_mac = m_netif->mac();
    req.sender_ip = m_netif->ip();
    req.target_mac = MacAddress::zero();
    req.target_ip = target_ip;

    m_netif->send_ethernet(MacAddress::broadcast(), ETHERTYPE_ARP, &req, sizeof(req));
}

void ArpEngine::send_reply(Ipv4Address target_ip, const MacAddress& target_mac) {
    ArpHeader rep{};
    rep.hw_type = htons(ARP_HW_ETHERNET);
    rep.proto_type = htons(ETHERTYPE_IPV4);
    rep.hw_len = 6;
    rep.proto_len = 4;
    rep.opcode = htons(ARP_OP_REPLY);
    rep.sender_mac = m_netif->mac();
    rep.sender_ip = m_netif->ip();
    rep.target_mac = target_mac;
    rep.target_ip = target_ip;

    m_netif->send_ethernet(target_mac, ETHERTYPE_ARP, &rep, sizeof(rep));
}

void ArpEngine::dump_table() const {
    klog_info("--- ARP Cache Table ---");
    for (size_t i = 0; i < ARP_TABLE_CAPACITY; ++i) {
        if (m_table[i].valid) {
            klog_info("  %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x",
                      m_table[i].ip.octet(0), m_table[i].ip.octet(1),
                      m_table[i].ip.octet(2), m_table[i].ip.octet(3),
                      m_table[i].mac.bytes[0], m_table[i].mac.bytes[1],
                      m_table[i].mac.bytes[2], m_table[i].mac.bytes[3],
                      m_table[i].mac.bytes[4], m_table[i].mac.bytes[5]);
        }
    }
}

} // namespace llamaos::net
