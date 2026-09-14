#include "tcp.hpp"
#include "net_interface.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - TCP Engine & Socket Implementation
// =============================================================================

namespace llamaos::net {

TcpEngine::Listener      TcpEngine::s_listeners[MAX_LISTENERS]{};
TcpConnection            TcpEngine::s_connections[MAX_TCP_CONNECTIONS]{};
sync::Spinlock           TcpEngine::s_tcp_lock;

void TcpEngine::init() {
    sync::SpinlockGuard guard(s_tcp_lock);
    for (size_t i = 0; i < MAX_LISTENERS; ++i) {
        s_listeners[i] = Listener{};
    }
    for (size_t i = 0; i < MAX_TCP_CONNECTIONS; ++i) {
        s_connections[i].reset();
    }
}

bool TcpEngine::listen(uint16_t port,
                       TcpDataCallback on_data,
                       TcpEventCallback on_connect,
                       TcpEventCallback on_close) {
    sync::SpinlockGuard guard(s_tcp_lock);
    for (size_t i = 0; i < MAX_LISTENERS; ++i) {
        if (!s_listeners[i].active) {
            s_listeners[i].port = port;
            s_listeners[i].on_data = on_data;
            s_listeners[i].on_connect = on_connect;
            s_listeners[i].on_close = on_close;
            s_listeners[i].active = true;
            klog_info("TCP: Listening on port %u", port);
            return true;
        }
    }
    return false;
}

uint16_t TcpEngine::calculate_tcp_checksum(Ipv4Address src_ip,
                                           Ipv4Address dst_ip,
                                           const void* tcp_segment,
                                           size_t len) {
    uint32_t sum = 0;

    // RFC 793 Pseudo-Header 16-bit words (little-endian storage compatible with calculate_checksum)
    sum += (src_ip.raw & 0xFFFF);
    sum += (src_ip.raw >> 16);
    sum += (dst_ip.raw & 0xFFFF);
    sum += (dst_ip.raw >> 16);
    sum += htons(static_cast<uint16_t>(IP_PROTO_TCP));
    sum += htons(static_cast<uint16_t>(len));

    return calculate_checksum(tcp_segment, len, sum);
}

void TcpEngine::handle_packet(NetInterface* netif,
                              Ipv4Address src_ip,
                              const uint8_t* payload,
                              size_t len) {
    if (!netif || !payload || len < sizeof(TcpHeader)) return;

    const TcpHeader* tcp = reinterpret_cast<const TcpHeader*>(payload);
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint8_t  hdr_len  = tcp->header_len();

    if (hdr_len < sizeof(TcpHeader) || hdr_len > len) return;

    klog_info("TCP: Inbound segment from %u.%u.%u.%u:%u -> :%u (flags=0x%02x, len=%u)",
              src_ip.octet(0), src_ip.octet(1), src_ip.octet(2), src_ip.octet(3),
              src_port, dst_port, static_cast<uint32_t>(tcp->flags), static_cast<uint32_t>(len));

    const uint8_t* data = payload + hdr_len;
    size_t data_len = len - hdr_len;

    TcpConnection* conn = nullptr;
    {
        sync::SpinlockGuard guard(s_tcp_lock);

        // 1. Look for existing connection matching 4-tuple
        for (size_t i = 0; i < MAX_TCP_CONNECTIONS; ++i) {
            if (s_connections[i].m_active &&
                s_connections[i].m_remote_ip == src_ip &&
                s_connections[i].m_remote_port == src_port &&
                s_connections[i].m_local_port == dst_port) {
                conn = &s_connections[i];
                break;
            }
        }

        // 2. If no connection exists and SYN is set, check if a listener exists on dst_port
        if (!conn && (tcp->flags & TCP_FLAG_SYN) && !(tcp->flags & TCP_FLAG_ACK)) {
            Listener* listener = nullptr;
            for (size_t i = 0; i < MAX_LISTENERS; ++i) {
                if (s_listeners[i].active && s_listeners[i].port == dst_port) {
                    listener = &s_listeners[i];
                    break;
                }
            }

            if (listener) {
                // Find free connection slot
                for (size_t i = 0; i < MAX_TCP_CONNECTIONS; ++i) {
                    if (!s_connections[i].m_active || s_connections[i].m_state == TcpState::Closed) {
                        conn = &s_connections[i];
                        conn->init(netif, src_ip, src_port, dst_port);
                        conn->set_callbacks(listener->on_data, listener->on_connect, listener->on_close);
                        conn->m_active = true;
                        break;
                    }
                }
            }
        }
    }

    if (conn) {
        conn->handle_segment(tcp, data, data_len);
    }
}

// -----------------------------------------------------------------------------
// TcpConnection Implementation
// -----------------------------------------------------------------------------

void TcpConnection::reset() {
    m_netif = nullptr;
    m_remote_ip = Ipv4Address{};
    m_remote_port = 0;
    m_local_port = 0;
    m_state = TcpState::Closed;
    m_seq_num = 0;
    m_ack_num = 0;
    m_remote_window = 8192;
    m_active = false;
    m_on_data = nullptr;
    m_on_connect = nullptr;
    m_on_close = nullptr;
}

void TcpConnection::init(NetInterface* netif,
                         Ipv4Address remote_ip,
                         uint16_t remote_port,
                         uint16_t local_port) {
    m_netif = netif;
    m_remote_ip = remote_ip;
    m_remote_port = remote_port;
    m_local_port = local_port;
    m_state = TcpState::Listen;
    m_seq_num = 100000;
    m_ack_num = 0;
    m_remote_window = 8192;
    m_active = true;
}

bool TcpConnection::send_segment(uint8_t flags, const void* payload, size_t payload_len) {
    if (!m_netif) return false;

    alignas(4) uint8_t buffer[1500];
    TcpHeader* tcp = reinterpret_cast<TcpHeader*>(buffer);

    tcp->src_port            = htons(m_local_port);
    tcp->dst_port            = htons(m_remote_port);
    tcp->seq_num             = htonl(m_seq_num);
    tcp->ack_num             = htonl(m_ack_num);
    tcp->data_offset         = (sizeof(TcpHeader) / 4) << 4;
    tcp->flags               = flags;
    tcp->window_size         = htons(16384);
    tcp->checksum            = 0;
    tcp->urgent_ptr          = 0;

    if (payload && payload_len > 0) {
        llamaos::memcpy(buffer + sizeof(TcpHeader), payload, payload_len);
    }

    size_t total_len = sizeof(TcpHeader) + payload_len;
    tcp->checksum = TcpEngine::calculate_tcp_checksum(m_netif->ip(), m_remote_ip, buffer, total_len);

    // Sequence number advancing
    m_seq_num += static_cast<uint32_t>(payload_len);
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        m_seq_num += 1;
    }

    return m_netif->send_ipv4(m_remote_ip, IP_PROTO_TCP, buffer, total_len);
}

bool TcpConnection::send(const void* data, size_t len) {
    if (m_state != TcpState::Established || !data || len == 0) {
        return false;
    }
    sync::SpinlockGuard guard(m_conn_lock);
    return send_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
}

void TcpConnection::close() {
    sync::SpinlockGuard guard(m_conn_lock);
    if (m_state == TcpState::Established) {
        m_state = TcpState::LastAck;
        send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK);
    } else {
        m_state = TcpState::Closed;
        m_active = false;
    }
}

void TcpConnection::handle_segment(const TcpHeader* tcp, const uint8_t* payload, size_t payload_len) {
    TcpEventCallback connect_cb = nullptr;
    TcpDataCallback  data_cb = nullptr;
    TcpEventCallback close_cb = nullptr;

    {
        sync::SpinlockGuard guard(m_conn_lock);

        uint32_t seg_seq = ntohl(tcp->seq_num);
        uint32_t seg_ack = ntohl(tcp->ack_num);
        (void)seg_ack;

        if (tcp->flags & TCP_FLAG_RST) {
            m_state = TcpState::Closed;
            m_active = false;
            close_cb = m_on_close;
        } else {
            switch (m_state) {
                case TcpState::Listen: {
                    if (tcp->flags & TCP_FLAG_SYN) {
                        m_ack_num = seg_seq + 1;
                        m_state = TcpState::SynReceived;
                        send_segment(TCP_FLAG_SYN | TCP_FLAG_ACK);
                        klog_info("TCP: Received SYN from %u.%u.%u.%u:%u, sent SYN-ACK",
                                  m_remote_ip.octet(0), m_remote_ip.octet(1),
                                  m_remote_ip.octet(2), m_remote_ip.octet(3), m_remote_port);
                    }
                    break;
                }

                case TcpState::SynReceived: {
                    if (tcp->flags & TCP_FLAG_ACK) {
                        m_state = TcpState::Established;
                        klog_info("TCP: Connection ESTABLISHED with %u.%u.%u.%u:%u",
                                  m_remote_ip.octet(0), m_remote_ip.octet(1),
                                  m_remote_ip.octet(2), m_remote_ip.octet(3), m_remote_port);
                        connect_cb = m_on_connect;
                    }
                    break;
                }

                case TcpState::Established: {
                    if (payload_len > 0) {
                        m_ack_num = seg_seq + static_cast<uint32_t>(payload_len);
                        send_segment(TCP_FLAG_ACK);
                        data_cb = m_on_data;
                    }

                    if (tcp->flags & TCP_FLAG_FIN) {
                        m_ack_num = seg_seq + (payload_len > 0 ? static_cast<uint32_t>(payload_len) : 0) + 1;
                        m_state = TcpState::LastAck;
                        send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK);
                        close_cb = m_on_close;
                    }
                    break;
                }

                case TcpState::LastAck: {
                    if (tcp->flags & TCP_FLAG_ACK) {
                        m_state = TcpState::Closed;
                        m_active = false;
                        klog_info("TCP: Connection CLOSED with %u.%u.%u.%u:%u",
                                  m_remote_ip.octet(0), m_remote_ip.octet(1),
                                  m_remote_ip.octet(2), m_remote_ip.octet(3), m_remote_port);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    if (connect_cb) connect_cb(this);
    if (data_cb)    data_cb(this, payload, payload_len);
    if (close_cb)   close_cb(this);
}

} // namespace llamaos::net
