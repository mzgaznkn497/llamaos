#pragma once

#include "net_types.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - Address Resolution Protocol (ARP) Cache & Resolution Engine
// =============================================================================

namespace llamaos::net {

class NetInterface;

class ArpEngine {
public:
    static constexpr size_t ARP_TABLE_CAPACITY = 32;

    struct ArpEntry {
        Ipv4Address ip{};
        MacAddress  mac{};
        bool        valid{false};
        uint64_t    timestamp{0};
    };

    explicit ArpEngine(NetInterface* netif) : m_netif(netif) {}

    void init();

    // Cache management
    bool lookup(Ipv4Address ip, MacAddress& out_mac);
    void insert(Ipv4Address ip, const MacAddress& mac);

    // Protocol operations
    void handle_packet(const uint8_t* payload, size_t len);
    void send_request(Ipv4Address target_ip);
    void send_reply(Ipv4Address target_ip, const MacAddress& target_mac);

    void dump_table() const;

private:
    NetInterface*  m_netif{nullptr};
    ArpEntry       m_table[ARP_TABLE_CAPACITY]{};
    sync::Spinlock m_lock;
};

} // namespace llamaos::net
