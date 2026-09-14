#include "icmp.hpp"
#include "net_interface.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - ICMP Engine Implementation
// =============================================================================

namespace llamaos::net {

void IcmpEngine::handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len) {
    if (!netif || !payload || len < sizeof(IcmpHeader)) return;

    const IcmpHeader* icmp = reinterpret_cast<const IcmpHeader*>(payload);

    // Verify ICMP checksum
    if (calculate_checksum(payload, len) != 0) {
        klog_warn("ICMP: Checksum error on incoming packet!");
        return;
    }

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST && icmp->code == 0) {
        // Prepare Echo Reply in static/local buffer
        alignas(4) uint8_t reply_buf[512];
        if (len > sizeof(reply_buf)) len = sizeof(reply_buf);

        llamaos::memcpy(reply_buf, payload, len);
        IcmpHeader* reply_hdr = reinterpret_cast<IcmpHeader*>(reply_buf);
        reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
        reply_hdr->code = 0;
        reply_hdr->checksum = 0;

        // Recalculate checksum
        reply_hdr->checksum = calculate_checksum(reply_buf, len);

        netif->send_ipv4(src_ip, IP_PROTO_ICMP, reply_buf, len);
        klog_info("ICMP: Echo Reply sent to %u.%u.%u.%u (seq=%u, len=%u)",
                  src_ip.octet(0), src_ip.octet(1), src_ip.octet(2), src_ip.octet(3),
                  ntohs(icmp->sequence), static_cast<uint32_t>(len));
    }
}

} // namespace llamaos::net
