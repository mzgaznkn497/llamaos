#include "udp.hpp"
#include "net_interface.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - UDP Engine Implementation
// =============================================================================

namespace llamaos::net {

UdpEngine::HandlerEntry UdpEngine::s_handlers[MAX_UDP_HANDLERS]{};
size_t UdpEngine::s_handler_count = 0;

void UdpEngine::register_handler(uint16_t port, UdpPacketHandler handler) {
    for (size_t i = 0; i < s_handler_count; ++i) {
        if (s_handlers[i].port == port) {
            s_handlers[i].handler = handler;
            return;
        }
    }
    if (s_handler_count < MAX_UDP_HANDLERS) {
        s_handlers[s_handler_count++] = {port, handler};
    }
}

void UdpEngine::handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len) {
    if (!netif || !payload || len < sizeof(UdpHeader)) return;

    const UdpHeader* udp = reinterpret_cast<const UdpHeader*>(payload);
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t ulen = ntohs(udp->length);

    if (ulen > len || ulen < sizeof(UdpHeader)) return;

    const uint8_t* data = payload + sizeof(UdpHeader);
    size_t data_len = ulen - sizeof(UdpHeader);

    for (size_t i = 0; i < s_handler_count; ++i) {
        if (s_handlers[i].port == dst_port && s_handlers[i].handler) {
            s_handlers[i].handler(netif, src_ip, src_port, dst_port, data, data_len);
            return;
        }
    }
}

bool UdpEngine::send(NetInterface* netif,
                     Ipv4Address dst_ip,
                     uint16_t src_port,
                     uint16_t dst_port,
                     const void* payload,
                     size_t len) {
    if (!netif || len > 1400) return false;

    alignas(4) uint8_t buffer[1500];
    UdpHeader* udp = reinterpret_cast<UdpHeader*>(buffer);

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(static_cast<uint16_t>(sizeof(UdpHeader) + len));
    udp->checksum = 0; // Checksum optional in IPv4 UDP

    if (payload && len > 0) {
        llamaos::memcpy(buffer + sizeof(UdpHeader), payload, len);
    }

    return netif->send_ipv4(dst_ip, IP_PROTO_UDP, buffer, sizeof(UdpHeader) + len);
}

} // namespace llamaos::net
