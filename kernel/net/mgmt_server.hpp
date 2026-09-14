#pragma once

#include "net_types.hpp"
#include "tcp.hpp"

// =============================================================================
// LlamaOS/A - Secure Remote Management Console Service (Port 2222)
// =============================================================================
// Provides multi-client, authenticated, isolated remote administration over TCP.
// Features:
// - Per-connection session tracking (no shared static buffers)
// - Role-based privilege gating: 'auth <token>' required for sensitive actions
// - Brute-force lockout and rate-limiting (disconnect on repeated failures)
// - Rejection of unauthenticated reboot/poweroff
// - Safe concurrent access for up to 16 simultaneous sessions
// =============================================================================

namespace llamaos::net {

class RemoteManagementServer {
public:
    static constexpr uint16_t DEFAULT_PORT      = 2222;
    static constexpr size_t   MAX_SESSIONS      = 16;
    static constexpr size_t   MAX_CMD_LEN       = 256;
    static constexpr uint32_t MAX_AUTH_FAILURES = 5;

    struct ClientSession {
        TcpConnection* conn{nullptr};
        char           cmd_buffer[MAX_CMD_LEN]{0};
        size_t         cmd_len{0};
        bool           authenticated{false};
        uint32_t       failed_auth_attempts{0};
        bool           active{false};
    };

    static void init(uint16_t port = DEFAULT_PORT);

    static void on_client_connect(TcpConnection* conn);
    static void on_client_data(TcpConnection* conn, const uint8_t* data, size_t len);
    static void on_client_close(TcpConnection* conn);

    static const char* admin_token() noexcept { return s_admin_token; }
    static void set_admin_token(const char* token);

private:
    static ClientSession* find_session(TcpConnection* conn);
    static ClientSession* allocate_session(TcpConnection* conn);
    static void free_session(TcpConnection* conn);
    static void process_command(ClientSession* session, const char* cmd);

    static ClientSession s_sessions[MAX_SESSIONS];
    static char          s_admin_token[64];
};

} // namespace llamaos::net
