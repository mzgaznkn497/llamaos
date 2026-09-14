#pragma once

#include "net_types.hpp"
#include "sync/spinlock.hpp"

// =============================================================================
// LlamaOS/A - Transmission Control Protocol (TCP) State Machine & Socket Layer
// =============================================================================

namespace llamaos::net {

class NetInterface;
class TcpConnection;

using TcpDataCallback = void (*)(TcpConnection* conn, const uint8_t* data, size_t len);
using TcpEventCallback = void (*)(TcpConnection* conn);

enum class TcpState {
    Closed,
    Listen,
    SynReceived,
    Established,
    CloseWait,
    LastAck,
    TimeWait
};

class TcpConnection {
public:
    TcpConnection() = default;

    void init(NetInterface* netif,
              Ipv4Address remote_ip,
              uint16_t remote_port,
              uint16_t local_port);

    bool send(const void* data, size_t len);
    void close();
    void reset();

    // State inquiries
    [[nodiscard]] TcpState state() const noexcept { return m_state; }
    [[nodiscard]] bool is_established() const noexcept { return m_state == TcpState::Established; }
    [[nodiscard]] Ipv4Address remote_ip() const noexcept { return m_remote_ip; }
    [[nodiscard]] uint16_t remote_port() const noexcept { return m_remote_port; }
    [[nodiscard]] uint16_t local_port() const noexcept { return m_local_port; }

    void set_callbacks(TcpDataCallback on_data, TcpEventCallback on_connect, TcpEventCallback on_close) {
        m_on_data = on_data;
        m_on_connect = on_connect;
        m_on_close = on_close;
    }

    void handle_segment(const TcpHeader* tcp, const uint8_t* payload, size_t payload_len);

private:
    friend class TcpEngine;

    bool send_segment(uint8_t flags, const void* payload = nullptr, size_t payload_len = 0);

    NetInterface*    m_netif{nullptr};
    Ipv4Address      m_remote_ip{};
    uint16_t         m_remote_port{0};
    uint16_t         m_local_port{0};
    TcpState         m_state{TcpState::Closed};
    uint32_t         m_seq_num{0};
    uint32_t         m_ack_num{0};
    uint16_t         m_remote_window{8192};
    bool             m_active{false};

    TcpDataCallback  m_on_data{nullptr};
    TcpEventCallback m_on_connect{nullptr};
    TcpEventCallback m_on_close{nullptr};
    sync::Spinlock   m_conn_lock;
};

class TcpEngine {
public:
    static constexpr size_t MAX_TCP_CONNECTIONS = 16;
    static constexpr size_t MAX_LISTENERS       = 8;

    struct Listener {
        uint16_t         port{0};
        TcpDataCallback  on_data{nullptr};
        TcpEventCallback on_connect{nullptr};
        TcpEventCallback on_close{nullptr};
        bool             active{false};
    };

    static void init();

    // Server socket listening
    static bool listen(uint16_t port,
                       TcpDataCallback on_data,
                       TcpEventCallback on_connect = nullptr,
                       TcpEventCallback on_close = nullptr);

    // Dispatches incoming TCP packet from IPv4 layer
    static void handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len);

    static uint16_t calculate_tcp_checksum(Ipv4Address src_ip,
                                           Ipv4Address dst_ip,
                                           const void* tcp_segment,
                                           size_t len);

private:
    static Listener       s_listeners[MAX_LISTENERS];
    static TcpConnection  s_connections[MAX_TCP_CONNECTIONS];
    static sync::Spinlock s_tcp_lock;
};

} // namespace llamaos::net
