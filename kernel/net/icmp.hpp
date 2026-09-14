#pragma once

#include "net_types.hpp"

// =============================================================================
// LlamaOS/A - Internet Control Message Protocol (ICMP) Engine
// =============================================================================

namespace llamaos::net {

class NetInterface;

class IcmpEngine {
public:
    static void handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len);
};

} // namespace llamaos::net
