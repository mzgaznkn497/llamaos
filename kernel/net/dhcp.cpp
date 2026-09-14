#include "dhcp.hpp"
#include "net_interface.hpp"
#include "udp.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - RFC 2131 DHCP Client Implementation (Blocker 1)
// =============================================================================

namespace llamaos::net {

struct [[gnu::packed]] DhcpHeader {
    uint8_t     op;
    uint8_t     htype;
    uint8_t     hlen;
    uint8_t     hops;
    uint32_t    xid;
    uint16_t    secs;
    uint16_t    flags;
    Ipv4Address ciaddr;
    Ipv4Address yiaddr;
    Ipv4Address siaddr;
    Ipv4Address giaddr;
    uint8_t     chaddr[16];
    char        sname[64];
    char        file[128];
    uint8_t     magic_cookie[4];
};

NetInterface* DhcpClient::s_netif = nullptr;
DhcpClient::State DhcpClient::s_state = DhcpClient::State::Init;
uint32_t      DhcpClient::s_xid = 0x4C4C414D; // "LLAM"
Ipv4Address   DhcpClient::s_offered_ip{};
Ipv4Address   DhcpClient::s_server_ip{};
DhcpLease     DhcpClient::s_lease{};

void DhcpClient::init(NetInterface* netif) {
    s_netif = netif;
    s_state = State::Init;
    s_lease = DhcpLease{};
    UdpEngine::register_handler(68, handle_udp);
}

void DhcpClient::start() {
    if (!s_netif) return;
    s_xid++;
    s_state = State::Init;
    s_lease = DhcpLease{};
    send_discover();
}

bool DhcpClient::request_lease(NetInterface* netif, size_t max_polls) {
    if (!netif) return false;
    s_netif = netif;
    s_state = State::Init;
    s_lease = DhcpLease{};
    s_xid++;
    send_discover();

    for (size_t p = 0; p < max_polls; ++p) {
        netif->poll();
        if (s_state == State::Bound) {
            return true;
        }
        // Delay (~5-10ms)
        for (int d = 0; d < 50000; ++d) {
            asm volatile("pause" ::: "memory");
        }
        // Retransmit if no offer received after 30 polls
        if (p == 30 && s_state == State::DiscoverSent) {
            klog_info("DHCP: Retransmitting DHCPDISCOVER on %s...", netif->name());
            send_discover();
        } else if (p == 60 && s_state == State::RequestSent) {
            klog_info("DHCP: Retransmitting DHCPREQUEST on %s...", netif->name());
            send_request();
        }
    }

    if (s_state != State::Bound) {
        s_state = State::Failed;
        klog_warn("DHCP: Lease acquisition timed out on interface %s", netif->name());
        return false;
    }
    return true;
}

void DhcpClient::send_discover() {
    alignas(4) uint8_t buf[512]{0};
    DhcpHeader* hdr = reinterpret_cast<DhcpHeader*>(buf);

    hdr->op    = 1; // BOOTREQUEST
    hdr->htype = 1; // Ethernet
    hdr->hlen  = 6;
    hdr->hops  = 0;
    hdr->xid   = htonl(s_xid);
    hdr->flags = htons(0x8000); // Broadcast
    llamaos::memcpy(hdr->chaddr, s_netif->mac().bytes, 6);

    // Magic Cookie: 99, 130, 83, 99 (0x63825363)
    hdr->magic_cookie[0] = 0x63;
    hdr->magic_cookie[1] = 0x82;
    hdr->magic_cookie[2] = 0x53;
    hdr->magic_cookie[3] = 0x63;

    size_t opt_idx = sizeof(DhcpHeader);

    // Option 53: DHCP Message Type = DHCPDISCOVER (1)
    buf[opt_idx++] = 53;
    buf[opt_idx++] = 1;
    buf[opt_idx++] = 1;

    // Option 55: Parameter Request List (Subnet Mask, Router, DNS, Lease Time, Renewal, Rebinding)
    buf[opt_idx++] = 55;
    buf[opt_idx++] = 6;
    buf[opt_idx++] = 1;  // Subnet Mask
    buf[opt_idx++] = 3;  // Router
    buf[opt_idx++] = 6;  // DNS
    buf[opt_idx++] = 51; // Lease Time
    buf[opt_idx++] = 58; // Renewal Time
    buf[opt_idx++] = 59; // Rebinding Time

    // Option 255: End
    buf[opt_idx++] = 255;

    s_state = State::DiscoverSent;
    klog_info("DHCP: Sending DHCPDISCOVER on %s (XID: 0x%08x)...", s_netif->name(), s_xid);
    UdpEngine::send(s_netif, Ipv4Address::broadcast(), 68, 67, buf, opt_idx);
}

void DhcpClient::send_request() {
    alignas(4) uint8_t buf[512]{0};
    DhcpHeader* hdr = reinterpret_cast<DhcpHeader*>(buf);

    hdr->op    = 1;
    hdr->htype = 1;
    hdr->hlen  = 6;
    hdr->hops  = 0;
    hdr->xid   = htonl(s_xid);
    hdr->flags = htons(0x8000);
    llamaos::memcpy(hdr->chaddr, s_netif->mac().bytes, 6);

    hdr->magic_cookie[0] = 0x63;
    hdr->magic_cookie[1] = 0x82;
    hdr->magic_cookie[2] = 0x53;
    hdr->magic_cookie[3] = 0x63;

    size_t opt_idx = sizeof(DhcpHeader);

    // Option 53: DHCP Message Type = DHCPREQUEST (3)
    buf[opt_idx++] = 53;
    buf[opt_idx++] = 1;
    buf[opt_idx++] = 3;

    // Option 50: Requested IP
    buf[opt_idx++] = 50;
    buf[opt_idx++] = 4;
    llamaos::memcpy(buf + opt_idx, &s_offered_ip.raw, 4);
    opt_idx += 4;

    // Option 54: Server Identifier
    buf[opt_idx++] = 54;
    buf[opt_idx++] = 4;
    llamaos::memcpy(buf + opt_idx, &s_server_ip.raw, 4);
    opt_idx += 4;

    // Option 255: End
    buf[opt_idx++] = 255;

    s_state = State::RequestSent;
    klog_info("DHCP: Sending DHCPREQUEST for %u.%u.%u.%u (Server: %u.%u.%u.%u)...",
              s_offered_ip.octet(0), s_offered_ip.octet(1),
              s_offered_ip.octet(2), s_offered_ip.octet(3),
              s_server_ip.octet(0), s_server_ip.octet(1),
              s_server_ip.octet(2), s_server_ip.octet(3));
    UdpEngine::send(s_netif, Ipv4Address::broadcast(), 68, 67, buf, opt_idx);
}

void DhcpClient::handle_udp(NetInterface* netif,
                           Ipv4Address src_ip,
                           uint16_t src_port,
                           uint16_t dst_port,
                           const uint8_t* payload,
                           size_t len) {
    (void)src_ip; (void)src_port; (void)dst_port;
    if (!netif || !payload || len < sizeof(DhcpHeader)) return;

    const DhcpHeader* hdr = reinterpret_cast<const DhcpHeader*>(payload);
    if (hdr->op != 2) return; // Must be BOOTREPLY
    if (ntohl(hdr->xid) != s_xid) return;

    // Check magic cookie
    if (hdr->magic_cookie[0] != 0x63 || hdr->magic_cookie[1] != 0x82 ||
        hdr->magic_cookie[2] != 0x53 || hdr->magic_cookie[3] != 0x63) {
        return;
    }

    // Parse options
    size_t opt_offset = sizeof(DhcpHeader);
    uint8_t msg_type = 0;
    Ipv4Address server_id{};
    Ipv4Address subnet_mask{};
    Ipv4Address router{};
    Ipv4Address dns{};
    uint32_t lease_time = 0;
    uint32_t renew_time = 0;
    uint32_t rebind_time = 0;

    while (opt_offset < len) {
        uint8_t opt = payload[opt_offset++];
        if (opt == 0) continue; // Pad
        if (opt == 255) break;  // End
        if (opt_offset >= len) break;

        uint8_t opt_len = payload[opt_offset++];
        if (opt_offset + opt_len > len) break;

        if (opt == 53 && opt_len == 1) {
            msg_type = payload[opt_offset];
        } else if (opt == 54 && opt_len == 4) {
            llamaos::memcpy(&server_id.raw, payload + opt_offset, 4);
        } else if (opt == 1 && opt_len == 4) {
            llamaos::memcpy(&subnet_mask.raw, payload + opt_offset, 4);
        } else if (opt == 3 && opt_len >= 4) {
            llamaos::memcpy(&router.raw, payload + opt_offset, 4);
        } else if (opt == 6 && opt_len >= 4) {
            llamaos::memcpy(&dns.raw, payload + opt_offset, 4);
        } else if (opt == 51 && opt_len == 4) {
            uint32_t raw_val = 0;
            llamaos::memcpy(&raw_val, payload + opt_offset, 4);
            lease_time = ntohl(raw_val);
        } else if (opt == 58 && opt_len == 4) {
            uint32_t raw_val = 0;
            llamaos::memcpy(&raw_val, payload + opt_offset, 4);
            renew_time = ntohl(raw_val);
        } else if (opt == 59 && opt_len == 4) {
            uint32_t raw_val = 0;
            llamaos::memcpy(&raw_val, payload + opt_offset, 4);
            rebind_time = ntohl(raw_val);
        }

        opt_offset += opt_len;
    }

    if (msg_type == 2 && s_state == State::DiscoverSent) { // DHCPOFFER
        s_offered_ip = hdr->yiaddr;
        s_server_ip = server_id;
        s_state = State::OfferReceived;
        klog_info("DHCP: Received DHCPOFFER for %u.%u.%u.%u from server %u.%u.%u.%u",
                  s_offered_ip.octet(0), s_offered_ip.octet(1),
                  s_offered_ip.octet(2), s_offered_ip.octet(3),
                  s_server_ip.octet(0), s_server_ip.octet(1),
                  s_server_ip.octet(2), s_server_ip.octet(3));
        send_request();
    } else if (msg_type == 5 && s_state == State::RequestSent) { // DHCPACK
        s_state = State::Bound;

        s_lease.ip = s_offered_ip;
        s_lease.subnet_mask = subnet_mask;
        s_lease.gateway = router;
        s_lease.dns = dns;
        s_lease.server_ip = s_server_ip;
        s_lease.lease_time_sec = lease_time;
        s_lease.renew_time_sec = renew_time;
        s_lease.rebind_time_sec = rebind_time;
        s_lease.valid = true;

        netif->set_ip(s_offered_ip);
        if (subnet_mask.raw != 0) netif->set_netmask(subnet_mask);
        if (router.raw != 0) netif->set_gateway(router); // Dynamic ARP query triggered
        if (dns.raw != 0) netif->set_dns(dns);

        klog_info("================================================================================");
        klog_info(" [PASS] DHCP Client Successfully Configured Network Interface '%s':", netif->name());
        klog_info("   IP Address   : %u.%u.%u.%u",
                  netif->ip().octet(0), netif->ip().octet(1), netif->ip().octet(2), netif->ip().octet(3));
        klog_info("   Subnet Mask  : %u.%u.%u.%u",
                  netif->netmask().octet(0), netif->netmask().octet(1), netif->netmask().octet(2), netif->netmask().octet(3));
        klog_info("   Gateway      : %u.%u.%u.%u (Dynamic ARP resolution triggered)",
                  netif->gateway().octet(0), netif->gateway().octet(1), netif->gateway().octet(2), netif->gateway().octet(3));
        klog_info("   DNS Server   : %u.%u.%u.%u",
                  netif->dns().octet(0), netif->dns().octet(1), netif->dns().octet(2), netif->dns().octet(3));
        klog_info("   Lease Time   : %u seconds (Renew: %u s, Rebind: %u s)",
                  lease_time, renew_time, rebind_time);
        klog_info("================================================================================");
    }
}

} // namespace llamaos::net
