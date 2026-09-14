#pragma once

#include "net_types.hpp"

// =============================================================================
// LlamaOS/A - RFC 2131 Dynamic Host Configuration Protocol (DHCP) Client
// =============================================================================
// Implements complete 4-step DHCP exchange:
// 1. DHCPDISCOVER (broadcast query with parameter request list)
// 2. DHCPOFFER (extract offered IP and server ID)
// 3. DHCPREQUEST (request the offered IP)
// 4. DHCPACK (extract IP, subnet mask, gateway, DNS, lease duration)
// =============================================================================

namespace llamaos::net {

class NetInterface;

struct DhcpLease {
    Ipv4Address ip{};
    Ipv4Address subnet_mask{};
    Ipv4Address gateway{};
    Ipv4Address dns{};
    Ipv4Address server_ip{};
    uint32_t    lease_time_sec{0};
    uint32_t    renew_time_sec{0};
    uint32_t    rebind_time_sec{0};
    bool        valid{false};
};

class DhcpClient {
public:
    enum class State {
        Init,
        DiscoverSent,
        OfferReceived,
        RequestSent,
        Bound,
        Failed
    };

    static void init(NetInterface* netif);
    static void start();
    static bool request_lease(NetInterface* netif, size_t max_polls = 100);
    static bool is_bound() noexcept { return s_state == State::Bound; }
    static State state() noexcept { return s_state; }
    static const DhcpLease& lease() noexcept { return s_lease; }

    static void handle_udp(NetInterface* netif,
                           Ipv4Address src_ip,
                           uint16_t src_port,
                           uint16_t dst_port,
                           const uint8_t* payload,
                           size_t len);

private:
    static void send_discover();
    static void send_request();

    static NetInterface* s_netif;
    static State         s_state;
    static uint32_t      s_xid;
    static Ipv4Address   s_offered_ip;
    static Ipv4Address   s_server_ip;
    static DhcpLease     s_lease;
};

} // namespace llamaos::net
