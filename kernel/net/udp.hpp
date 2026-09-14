#pragma once

#include "net_types.hpp"

// =============================================================================
// LlamaOS/A - User Datagram Protocol (UDP) Engine
// =============================================================================

namespace llamaos::net {

class NetInterface;

using UdpPacketHandler = void (*)(NetInterface* netif,
                                  Ipv4Address src_ip,
                                  uint16_t src_port,
                                  uint16_t dst_port,
                                  const uint8_t* payload,
                                  size_t len);

class UdpEngine {
public:
    static constexpr size_t MAX_UDP_HANDLERS = 8;

    struct HandlerEntry {
        uint16_t         port{0};
        UdpPacketHandler handler{nullptr};
    };

    static void register_handler(uint16_t port, UdpPacketHandler handler);

    static void handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len);

    static bool send(NetInterface* netif,
                     Ipv4Address dst_ip,
                     uint16_t src_port,
                     uint16_t dst_port,
                     const void* payload,
                     size_t len);

private:
    static HandlerEntry s_handlers[MAX_UDP_HANDLERS];
    static size_t       s_handler_count;
};

} // namespace llamaos::net
