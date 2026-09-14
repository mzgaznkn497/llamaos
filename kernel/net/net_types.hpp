#pragma once

#include "core/types.hpp"
#include "core/string.hpp"

// =============================================================================
// LlamaOS/A - TCP/IP Protocol Suite Definitions & Byte-Order Conversions
// =============================================================================
// Packed network headers for Ethernet II, ARP, IPv4, ICMP, UDP, and TCP.
// Includes RFC 1071 16-bit one's complement checksum calculation.
// =============================================================================

namespace llamaos::net {

// Endianness conversion helpers
[[nodiscard]] inline constexpr uint16_t htons(uint16_t val) noexcept {
    return __builtin_bswap16(val);
}

[[nodiscard]] inline constexpr uint16_t ntohs(uint16_t val) noexcept {
    return __builtin_bswap16(val);
}

[[nodiscard]] inline constexpr uint32_t htonl(uint32_t val) noexcept {
    return __builtin_bswap32(val);
}

[[nodiscard]] inline constexpr uint32_t ntohl(uint32_t val) noexcept {
    return __builtin_bswap32(val);
}

// -----------------------------------------------------------------------------
// MAC Address (6 bytes)
// -----------------------------------------------------------------------------
struct [[gnu::packed]] MacAddress {
    uint8_t bytes[6]{0};

    constexpr MacAddress() = default;
    constexpr MacAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
        : bytes{b0, b1, b2, b3, b4, b5} {}

    static constexpr MacAddress broadcast() noexcept {
        return MacAddress(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    }

    static constexpr MacAddress zero() noexcept {
        return MacAddress(0, 0, 0, 0, 0, 0);
    }

    [[nodiscard]] bool is_broadcast() const noexcept {
        return bytes[0] == 0xFF && bytes[1] == 0xFF && bytes[2] == 0xFF &&
               bytes[3] == 0xFF && bytes[4] == 0xFF && bytes[5] == 0xFF;
    }

    [[nodiscard]] bool is_zero() const noexcept {
        return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 &&
               bytes[3] == 0 && bytes[4] == 0 && bytes[5] == 0;
    }

    bool operator==(const MacAddress& o) const noexcept {
        for (size_t i = 0; i < 6; ++i) {
            if (bytes[i] != o.bytes[i]) return false;
        }
        return true;
    }

    bool operator!=(const MacAddress& o) const noexcept { return !(*this == o); }
};
static_assert(sizeof(MacAddress) == 6, "MacAddress must be 6 bytes");

// -----------------------------------------------------------------------------
// IPv4 Address (4 bytes, stored in big-endian network byte order)
// -----------------------------------------------------------------------------
struct [[gnu::packed]] Ipv4Address {
    uint32_t raw{0};

    constexpr Ipv4Address() = default;
    constexpr explicit Ipv4Address(uint32_t raw_be) : raw(raw_be) {}
    constexpr Ipv4Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : raw((static_cast<uint32_t>(d) << 24) |
              (static_cast<uint32_t>(c) << 16) |
              (static_cast<uint32_t>(b) << 8)  |
              (static_cast<uint32_t>(a))) {}

    [[nodiscard]] uint8_t octet(size_t index) const noexcept {
        return static_cast<uint8_t>((raw >> (index * 8)) & 0xFF);
    }

    static constexpr Ipv4Address broadcast() noexcept {
        return Ipv4Address(0xFFFFFFFF);
    }

    static constexpr Ipv4Address any() noexcept {
        return Ipv4Address(0x00000000);
    }

    bool operator==(const Ipv4Address& o) const noexcept { return raw == o.raw; }
    bool operator!=(const Ipv4Address& o) const noexcept { return raw != o.raw; }
};
static_assert(sizeof(Ipv4Address) == 4, "Ipv4Address must be 4 bytes");

// -----------------------------------------------------------------------------
// Ethernet II Frame Header (14 bytes)
// -----------------------------------------------------------------------------
inline constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
inline constexpr uint16_t ETHERTYPE_ARP  = 0x0806;

struct [[gnu::packed]] EthernetHeader {
    MacAddress dest;
    MacAddress src;
    uint16_t   ethertype; // Big-endian
};
static_assert(sizeof(EthernetHeader) == 14, "EthernetHeader must be 14 bytes");

// -----------------------------------------------------------------------------
// ARP Header (28 bytes)
// -----------------------------------------------------------------------------
inline constexpr uint16_t ARP_HW_ETHERNET = 0x0001;
inline constexpr uint16_t ARP_OP_REQUEST  = 0x0001;
inline constexpr uint16_t ARP_OP_REPLY    = 0x0002;

struct [[gnu::packed]] ArpHeader {
    uint16_t   hw_type;      // 0x0001 = Ethernet
    uint16_t   proto_type;   // 0x0800 = IPv4
    uint8_t    hw_len;       // 6
    uint8_t    proto_len;    // 4
    uint16_t   opcode;       // 1 = Request, 2 = Reply
    MacAddress sender_mac;
    Ipv4Address sender_ip;
    MacAddress target_mac;
    Ipv4Address target_ip;
};
static_assert(sizeof(ArpHeader) == 28, "ArpHeader must be 28 bytes");

// -----------------------------------------------------------------------------
// IPv4 Header (20 bytes baseline without options)
// -----------------------------------------------------------------------------
inline constexpr uint8_t IP_PROTO_ICMP = 1;
inline constexpr uint8_t IP_PROTO_TCP  = 6;
inline constexpr uint8_t IP_PROTO_UDP  = 17;

struct [[gnu::packed]] Ipv4Header {
    uint8_t     ver_ihl;      // Version (4 bits) + IHL (4 bits)
    uint8_t     dscp_ecn;
    uint16_t    total_length; // Big-endian
    uint16_t    id;
    uint16_t    flags_frag;
    uint8_t     ttl;
    uint8_t     protocol;     // 1=ICMP, 6=TCP, 17=UDP
    uint16_t    checksum;
    Ipv4Address src_ip;
    Ipv4Address dst_ip;

    [[nodiscard]] uint8_t version() const noexcept { return ver_ihl >> 4; }
    [[nodiscard]] uint8_t header_len() const noexcept { return (ver_ihl & 0x0F) * 4; }
};
static_assert(sizeof(Ipv4Header) == 20, "Ipv4Header must be 20 bytes");

// -----------------------------------------------------------------------------
// ICMP Header (8 bytes baseline)
// -----------------------------------------------------------------------------
inline constexpr uint8_t ICMP_TYPE_ECHO_REPLY   = 0;
inline constexpr uint8_t ICMP_TYPE_ECHO_REQUEST = 8;

struct [[gnu::packed]] IcmpHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
};
static_assert(sizeof(IcmpHeader) == 8, "IcmpHeader must be 8 bytes");

// -----------------------------------------------------------------------------
// UDP Header (8 bytes)
// -----------------------------------------------------------------------------
struct [[gnu::packed]] UdpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};
static_assert(sizeof(UdpHeader) == 8, "UdpHeader must be 8 bytes");

// -----------------------------------------------------------------------------
// TCP Header (20 bytes baseline)
// -----------------------------------------------------------------------------
inline constexpr uint8_t TCP_FLAG_FIN = 0x01;
inline constexpr uint8_t TCP_FLAG_SYN = 0x02;
inline constexpr uint8_t TCP_FLAG_RST = 0x04;
inline constexpr uint8_t TCP_FLAG_PSH = 0x08;
inline constexpr uint8_t TCP_FLAG_ACK = 0x10;
inline constexpr uint8_t TCP_FLAG_URG = 0x20;

struct [[gnu::packed]] TcpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset; // High 4 bits: header length in 32-bit words
    uint8_t  flags;       // TCP flags (SYN, ACK, FIN, etc.)
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;

    [[nodiscard]] uint8_t header_len() const noexcept { return (data_offset >> 4) * 4; }
};
static_assert(sizeof(TcpHeader) == 20, "TcpHeader must be 20 bytes");

// -----------------------------------------------------------------------------
// RFC 1071 One's Complement Internet Checksum Calculator
// -----------------------------------------------------------------------------
inline uint16_t calculate_checksum(const void* data, size_t len, uint32_t initial_sum = 0) noexcept {
    const uint16_t* ptr = reinterpret_cast<const uint16_t*>(data);
    uint32_t sum = initial_sum;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}

} // namespace llamaos::net
