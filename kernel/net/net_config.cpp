#include "net_config.hpp"
#include "dhcp.hpp"
#include "mgmt_server.hpp"
#include "fs/vfs.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Network Configuration Manager Implementation (Blockers 1 & 9)
// =============================================================================

namespace llamaos::net {

NetInterface* NetConfig::s_primary_interface = nullptr;

NetInterface* NetConfig::primary_interface() noexcept {
    if (s_primary_interface) return s_primary_interface;
    return NetInterface::default_interface();
}

void NetConfig::set_primary_interface(NetInterface* iface) noexcept {
    s_primary_interface = iface;
}

bool NetConfig::parse_ipv4(const char* str, Ipv4Address& out_ip) {
    if (!str) return false;

    uint32_t octets[4] = {0};
    size_t octet_idx = 0;
    uint32_t current_val = 0;
    bool has_digits = false;

    for (size_t i = 0; str[i] != '\0' && str[i] != '\r' && str[i] != '\n' && str[i] != ' ' && str[i] != '\t'; ++i) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            current_val = (current_val * 10) + static_cast<uint32_t>(c - '0');
            if (current_val > 255) return false;
            has_digits = true;
        } else if (c == '.') {
            if (!has_digits || octet_idx >= 3) return false;
            octets[octet_idx++] = current_val;
            current_val = 0;
            has_digits = false;
        } else {
            return false;
        }
    }

    if (!has_digits || octet_idx != 3) return false;
    octets[octet_idx] = current_val;

    out_ip = Ipv4Address(
        static_cast<uint8_t>(octets[0]),
        static_cast<uint8_t>(octets[1]),
        static_cast<uint8_t>(octets[2]),
        static_cast<uint8_t>(octets[3])
    );
    return true;
}

static bool string_equals_ignore_case(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
    return true;
}

bool NetConfig::parse_config_file(const char* path, NetworkConfigFile& out_cfg) {
    out_cfg = NetworkConfigFile{};

    int fd = fs::Vfs::open(path, fs::O_RDONLY);
    if (fd < 0) {
        return false;
    }

    alignas(4) char buf[1024];
    llamaos::memset(buf, 0, sizeof(buf));
    int64_t bytes_read = fs::Vfs::read(fd, buf, sizeof(buf) - 1);
    fs::Vfs::close(fd);

    if (bytes_read <= 0) {
        return false;
    }

    out_cfg.file_present = true;
    const char* ptr = buf;

    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') ptr++;
        if (!*ptr) break;

        // Skip comment lines
        if (*ptr == '#' || *ptr == ';') {
            while (*ptr && *ptr != '\n') ptr++;
            continue;
        }

        // Check key-value pairs
        if (string_equals_ignore_case(ptr, "DHCP=", 5)) {
            const char* val = ptr + 5;
            while (*val == ' ') val++;
            if (string_equals_ignore_case(val, "no", 2) || string_equals_ignore_case(val, "0", 1) ||
                string_equals_ignore_case(val, "false", 5)) {
                out_cfg.dhcp_enabled = false;
            } else {
                out_cfg.dhcp_enabled = true;
            }
        } else if (string_equals_ignore_case(ptr, "mode=", 5)) {
            const char* val = ptr + 5;
            while (*val == ' ') val++;
            if (string_equals_ignore_case(val, "static", 6)) {
                out_cfg.dhcp_enabled = false;
            } else {
                out_cfg.dhcp_enabled = true;
            }
        } else if (string_equals_ignore_case(ptr, "STATIC_IP=", 10)) {
            parse_ipv4(ptr + 10, out_cfg.static_ip);
        } else if (string_equals_ignore_case(ptr, "ip=", 3)) {
            parse_ipv4(ptr + 3, out_cfg.static_ip);
        } else if (string_equals_ignore_case(ptr, "NETMASK=", 8)) {
            parse_ipv4(ptr + 8, out_cfg.netmask);
        } else if (string_equals_ignore_case(ptr, "netmask=", 8)) {
            parse_ipv4(ptr + 8, out_cfg.netmask);
        } else if (string_equals_ignore_case(ptr, "GATEWAY=", 8)) {
            parse_ipv4(ptr + 8, out_cfg.gateway);
        } else if (string_equals_ignore_case(ptr, "gateway=", 8)) {
            parse_ipv4(ptr + 8, out_cfg.gateway);
        } else if (string_equals_ignore_case(ptr, "DNS=", 4)) {
            parse_ipv4(ptr + 4, out_cfg.dns);
        } else if (string_equals_ignore_case(ptr, "dns=", 4)) {
            parse_ipv4(ptr + 4, out_cfg.dns);
        } else if (string_equals_ignore_case(ptr, "MGMT_PORT=", 10)) {
            const char* p = ptr + 10;
            uint16_t port = 0;
            while (*p >= '0' && *p <= '9') {
                port = static_cast<uint16_t>(port * 10 + (*p - '0'));
                p++;
            }
            if (port > 0) out_cfg.mgmt_port = port;
        }

        while (*ptr && *ptr != '\n') ptr++;
    }

    return true;
}

bool NetConfig::configure_interface(NetInterface* netif, const NetworkConfigFile& cfg) {
    if (!netif) return false;

    // 1. Primary path: DHCP Lease Acquisition
    if (cfg.dhcp_enabled) {
        klog_info("NetConfig: Requesting dynamic DHCP lease on '%s'...", netif->name());
        bool dhcp_ok = DhcpClient::request_lease(netif, 80);
        if (dhcp_ok) {
            klog_info("NetConfig: Successfully configured '%s' via DHCP (IP: %u.%u.%u.%u, GW: %u.%u.%u.%u)",
                      netif->name(),
                      netif->ip().octet(0), netif->ip().octet(1), netif->ip().octet(2), netif->ip().octet(3),
                      netif->gateway().octet(0), netif->gateway().octet(1), netif->gateway().octet(2), netif->gateway().octet(3));
            return true;
        }
        klog_warn("NetConfig: DHCP lease request failed or timed out on '%s'.", netif->name());
    }

    // 2. Deterministic static fallback if configured
    if (cfg.static_ip.raw != 0) {
        netif->set_ip(cfg.static_ip);
        if (cfg.netmask.raw != 0) netif->set_netmask(cfg.netmask);
        if (cfg.gateway.raw != 0) netif->set_gateway(cfg.gateway);
        if (cfg.dns.raw != 0) netif->set_dns(cfg.dns);

        klog_info("================================================================================");
        klog_info(" [PASS] NetConfig: Applied Static Fallback Configuration to '%s':", netif->name());
        klog_info("   Static IP    : %u.%u.%u.%u",
                  cfg.static_ip.octet(0), cfg.static_ip.octet(1), cfg.static_ip.octet(2), cfg.static_ip.octet(3));
        klog_info("   Netmask      : %u.%u.%u.%u",
                  cfg.netmask.octet(0), cfg.netmask.octet(1), cfg.netmask.octet(2), cfg.netmask.octet(3));
        klog_info("   Default GW   : %u.%u.%u.%u (Dynamic ARP resolution active)",
                  cfg.gateway.octet(0), cfg.gateway.octet(1), cfg.gateway.octet(2), cfg.gateway.octet(3));
        klog_info("   DNS Server   : %u.%u.%u.%u",
                  cfg.dns.octet(0), cfg.dns.octet(1), cfg.dns.octet(2), cfg.dns.octet(3));
        klog_info("================================================================================");
        return true;
    }

    klog_info("NetConfig: Interface '%s' remains unconfigured (awaiting external DHCP or manual assignment).", netif->name());
    return false;
}

size_t NetConfig::configure_all() {
    // 1. Enumerate all VirtIO network devices across PCI topology
    size_t dev_count = drivers::virtio::VirtioNetDevice::probe_all();
    if (dev_count == 0) {
        klog_info("NetConfig: Zero network adapters detected on PCI bus.");
        return 0;
    }

    klog_info("================================================================================");
    klog_info("NetConfig: Enumerated %u VirtIO network device(s) on PCI bus", static_cast<uint32_t>(dev_count));

    // 2. Parse persistent configuration file from disk if present
    NetworkConfigFile cfg{};
    if (!parse_config_file("/config/network.cfg", cfg)) {
        parse_config_file("/etc/network.cfg", cfg);
    }
    if (cfg.file_present) {
        klog_info("NetConfig: Found persistent configuration (DHCP=%s, Static=%u.%u.%u.%u)",
                  cfg.dhcp_enabled ? "yes" : "no",
                  cfg.static_ip.octet(0), cfg.static_ip.octet(1), cfg.static_ip.octet(2), cfg.static_ip.octet(3));
    } else {
        klog_info("NetConfig: No network.cfg found on disk. Defaulting to full DHCP mode.");
        cfg.dhcp_enabled = true;
    }

    // 3. Register and configure each network interface
    for (size_t i = 0; i < dev_count && i < NetInterface::MAX_INTERFACES; ++i) {
        auto* dev = drivers::virtio::VirtioNetDevice::get_device(i);
        if (!dev) continue;

        char if_name[16];
        if_name[0] = 'n';
        if_name[1] = 'e';
        if_name[2] = 't';
        if_name[3] = static_cast<char>('0' + i);
        if_name[4] = '\0';

        auto* netif = NetInterface::register_device(dev, if_name);
        if (!netif) {
            klog_error("NetConfig: Failed to register interface '%s'!", if_name);
            continue;
        }

        const auto& mac = netif->mac();
        klog_info("NetConfig: Registered interface '%s' (MAC: %02x:%02x:%02x:%02x:%02x:%02x, I/O: 0x%04x)",
                  if_name, mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3], mac.bytes[4], mac.bytes[5],
                  dev->io_base());

        // Configure interface (DHCP primary, static fallback)
        // For multi-NIC droplet: configure primary on net0; net1 is initialized and ready
        if (i == 0) {
            configure_interface(netif, cfg);
            s_primary_interface = netif;
        } else {
            klog_info("NetConfig: Secondary interface '%s' registered and online for VPC/private network traffic.", if_name);
        }
    }

    // 4. Initialize Remote Management Server on primary interface
    uint16_t mgmt_port = cfg.mgmt_port ? cfg.mgmt_port : 2222;
    RemoteManagementServer::init(mgmt_port);
    klog_info(" [PASS] Remote Management Service listening on TCP port %u", mgmt_port);

    klog_info("================================================================================");
    return NetInterface::interface_count();
}

} // namespace llamaos::net
