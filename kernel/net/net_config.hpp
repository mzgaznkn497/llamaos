#pragma once

#include "core/types.hpp"
#include "net/net_interface.hpp"

// =============================================================================
// LlamaOS/A - Network Configuration Manager (Blockers 1 & 9)
// =============================================================================
// Manages multi-NIC discovery, deterministic interface selection, and automated
// network configuration:
// 1. Probes all VirtIO-Net devices on PCI bus (supporting multiple NICs e.g. net0, net1)
// 2. Extracts MAC address and hardware identity for each device
// 3. Implements deterministic primary-interface policy (net0 as default primary)
// 4. Primary path: RFC 2131 DHCP lease acquisition (DHCPDISCOVER -> ACK)
// 5. Fallback path: optional deterministic static config parsed from /config/network.cfg
// 6. Zero hardcoded IPs, netmasks, gateways, or gateway MACs in production code
// =============================================================================

namespace llamaos::net {

struct NetworkConfigFile {
    bool        dhcp_enabled{true};
    Ipv4Address static_ip{};
    Ipv4Address netmask{};
    Ipv4Address gateway{};
    Ipv4Address dns{};
    uint16_t    mgmt_port{2222};
    bool        file_present{false};
};

class NetConfig {
public:
    // Discovers network hardware, binds interfaces, and performs auto-configuration
    static size_t configure_all();

    // Parse dotted-quad IPv4 string (e.g. "159.223.74.128")
    static bool parse_ipv4(const char* str, Ipv4Address& out_ip);

    // Primary interface policy
    static NetInterface* primary_interface() noexcept;
    static void set_primary_interface(NetInterface* iface) noexcept;

    // Configuration file parser
    static bool parse_config_file(const char* path, NetworkConfigFile& out_cfg);

private:
    static bool configure_interface(NetInterface* netif, const NetworkConfigFile& cfg);
    static NetInterface* s_primary_interface;
};

} // namespace llamaos::net
