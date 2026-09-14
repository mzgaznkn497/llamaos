#include "mgmt_server.hpp"
#include "core/power.hpp"
#include "core/string.hpp"
#include "core/kprint.hpp"
#include "fs/vfs.hpp"
#include "memory/pmm.hpp"
#include "storage/storage_manager.hpp"
#include "userland/process.hpp"
#include "arch/x86_64/cpu/timer.hpp"

// =============================================================================
// LlamaOS/A - Secure Remote Management Console Service Implementation
// =============================================================================

namespace llamaos::net {

RemoteManagementServer::ClientSession RemoteManagementServer::s_sessions[MAX_SESSIONS]{};
char RemoteManagementServer::s_admin_token[64] = "llamaos-admin-secure";

static void send_str(TcpConnection* conn, const char* str) {
    if (conn && str) {
        conn->send(str, llamaos::strlen(str));
    }
}

static void send_dec(TcpConnection* conn, uint64_t val) {
    if (!conn) return;
    if (val == 0) {
        conn->send("0", 1);
        return;
    }
    char buf[32];
    size_t idx = 0;
    while (val > 0) {
        buf[idx++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    for (size_t i = 0; i < idx / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = tmp;
    }
    buf[idx] = '\0';
    conn->send(buf, idx);
}

void RemoteManagementServer::set_admin_token(const char* token) {
    if (token) {
        llamaos::strncpy(s_admin_token, token, sizeof(s_admin_token) - 1);
        s_admin_token[sizeof(s_admin_token) - 1] = '\0';
    }
}

RemoteManagementServer::ClientSession* RemoteManagementServer::find_session(TcpConnection* conn) {
    if (!conn) return nullptr;
    for (size_t i = 0; i < MAX_SESSIONS; ++i) {
        if (s_sessions[i].active && s_sessions[i].conn == conn) {
            return &s_sessions[i];
        }
    }
    return nullptr;
}

RemoteManagementServer::ClientSession* RemoteManagementServer::allocate_session(TcpConnection* conn) {
    if (!conn) return nullptr;
    ClientSession* existing = find_session(conn);
    if (existing) return existing;

    for (size_t i = 0; i < MAX_SESSIONS; ++i) {
        if (!s_sessions[i].active) {
            s_sessions[i].conn = conn;
            s_sessions[i].cmd_len = 0;
            s_sessions[i].cmd_buffer[0] = '\0';
            s_sessions[i].authenticated = false;
            s_sessions[i].failed_auth_attempts = 0;
            s_sessions[i].active = true;
            return &s_sessions[i];
        }
    }
    return nullptr;
}

void RemoteManagementServer::free_session(TcpConnection* conn) {
    if (!conn) return;
    for (size_t i = 0; i < MAX_SESSIONS; ++i) {
        if (s_sessions[i].active && s_sessions[i].conn == conn) {
            s_sessions[i].active = false;
            s_sessions[i].conn = nullptr;
            s_sessions[i].cmd_len = 0;
            s_sessions[i].authenticated = false;
            s_sessions[i].failed_auth_attempts = 0;
            return;
        }
    }
}

void RemoteManagementServer::init(uint16_t port) {
    for (size_t i = 0; i < MAX_SESSIONS; ++i) {
        s_sessions[i].active = false;
        s_sessions[i].conn = nullptr;
    }

    // Try loading persistent management token from /config/mgmt.token
    int fd = fs::Vfs::open("/config/mgmt.token", fs::O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        llamaos::memset(buf, 0, sizeof(buf));
        int64_t n = fs::Vfs::read(fd, buf, sizeof(buf) - 1);
        fs::Vfs::close(fd);
        if (n > 0) {
            while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) {
                buf[--n] = '\0';
            }
            if (n > 0) {
                set_admin_token(buf);
                klog_info("RemoteManagementServer: Loaded admin token from /config/mgmt.token");
            }
        }
    }

    TcpEngine::listen(port, on_client_data, on_client_connect, on_client_close);
    klog_info("RemoteManagementServer: Listening on TCP port %u (auth-gated, multi-session)", port);
}

void RemoteManagementServer::on_client_connect(TcpConnection* conn) {
    if (!conn) return;
    ClientSession* session = allocate_session(conn);
    if (!session) {
        send_str(conn, "Server busy: maximum management sessions reached.\r\n");
        conn->close();
        return;
    }

    send_str(conn,
        "\r\n============================================================\r\n"
        " LlamaOS/A Secure Remote Management Console v2.0\r\n"
        " Kernel: LlamaOS/A x86-64 | Direct-Boot VPS Enterprise Edition\r\n"
        "============================================================\r\n"
        "Access restricted. Authenticate with 'auth <token>'.\r\n"
        "Type 'help' for command list.\r\n"
        "llamaos> ");
}

void RemoteManagementServer::on_client_close(TcpConnection* conn) {
    free_session(conn);
}

void RemoteManagementServer::on_client_data(TcpConnection* conn, const uint8_t* data, size_t len) {
    if (!conn || !data || len == 0) return;
    ClientSession* session = find_session(conn);
    if (!session) {
        session = allocate_session(conn);
        if (!session) return;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '\r') {
            continue;
        } else if (c == '\n') {
            if (session->cmd_len > 0) {
                session->cmd_buffer[session->cmd_len] = '\0';
                process_command(session, session->cmd_buffer);
                session->cmd_len = 0;
            } else {
                send_str(conn, session->authenticated ? "llamaos# " : "llamaos> ");
            }
        } else if (c == '\b' || c == 0x7F) {
            if (session->cmd_len > 0) {
                session->cmd_len--;
            }
        } else if (session->cmd_len < MAX_CMD_LEN - 1) {
            session->cmd_buffer[session->cmd_len++] = c;
        }
    }
}

void RemoteManagementServer::process_command(ClientSession* session, const char* cmd) {
    if (!session || !session->conn || !cmd) return;
    auto* conn = session->conn;

    while (*cmd == ' ') cmd++;

    if (llamaos::strcmp(cmd, "help") == 0) {
        send_str(conn,
            "\r\nAvailable Management Commands:\r\n"
            "  help          - Display this command list\r\n"
            "  auth <token>  - Authenticate for privileged administrative access\r\n"
            "  ping          - Connectivity test (echoes PONG)\r\n"
            "  exit          - Terminate management session\r\n");
        if (session->authenticated) {
            send_str(conn,
                "  status        - System status & uptime telemetry [Privileged]\r\n"
                "  mem           - Physical memory statistics (PMM) [Privileged]\r\n"
                "  ps            - Active process & thread table    [Privileged]\r\n"
                "  storage       - Registered block devices & GPT   [Privileged]\r\n"
                "  reboot        - Gracefully restart system        [Privileged]\r\n"
                "  poweroff      - Power down system (ACPI/QEMU)    [Privileged]\r\n"
                "llamaos# ");
        } else {
            send_str(conn,
                "  (Administrative status/mem/ps/storage/reboot/poweroff require 'auth <token>')\r\n"
                "llamaos> ");
        }
        return;
    }

    if (llamaos::strncmp(cmd, "auth ", 5) == 0) {
        const char* supplied_token = cmd + 5;
        while (*supplied_token == ' ') supplied_token++;

        if (llamaos::strcmp(supplied_token, s_admin_token) == 0) {
            session->authenticated = true;
            session->failed_auth_attempts = 0;
            klog_info("RemoteManagementServer: Session authenticated successfully.");
            send_str(conn, "\r\n[AUTH OK] Authentication successful. Administrative commands unlocked.\r\nllamaos# ");
        } else {
            session->failed_auth_attempts++;
            klog_warn("RemoteManagementServer: Authentication failed (attempt %u/%u).",
                      session->failed_auth_attempts, MAX_AUTH_FAILURES);
            if (session->failed_auth_attempts >= MAX_AUTH_FAILURES) {
                send_str(conn, "\r\n[AUTH LOCKED] Maximum authentication attempts exceeded. Session terminated.\r\n");
                conn->close();
                free_session(conn);
                return;
            }
            send_str(conn, "\r\n[AUTH FAIL] Invalid management token.\r\nllamaos> ");
        }
        return;
    }

    if (llamaos::strcmp(cmd, "ping") == 0) {
        send_str(conn, "\r\nPONG\r\n");
        send_str(conn, session->authenticated ? "llamaos# " : "llamaos> ");
        return;
    }

    if (llamaos::strcmp(cmd, "exit") == 0) {
        send_str(conn, "\r\nClosing management session. Goodbye.\r\n");
        conn->close();
        free_session(conn);
        return;
    }

    // All subsequent commands require authentication
    if (!session->authenticated) {
        send_str(conn, "\r\n[PERMISSION DENIED] Command '");
        send_str(conn, cmd);
        send_str(conn, "' requires administrative privilege. Type 'auth <token>'.\r\nllamaos> ");
        return;
    }

    // Authenticated Commands
    if (llamaos::strcmp(cmd, "status") == 0) {
        uint64_t ticks = arch::x86_64::Timer::ticks();
        uint64_t uptime_sec = ticks / 100;

        send_str(conn, "\r\n[SYSTEM STATUS]\r\n  OS Name     : LlamaOS/A Production Kernel\r\n  Architecture: x86-64 (Long Mode, Ring 0/Ring 3)\r\n  Uptime      : ");
        send_dec(conn, uptime_sec);
        send_str(conn, " seconds (");
        send_dec(conn, ticks);
        send_str(conn, " ticks)\r\n  Status      : HEALTHY (All Subsystems Operational)\r\nllamaos# ");
    } else if (llamaos::strcmp(cmd, "mem") == 0) {
        size_t total_pages = memory::g_pmm.total_frames();
        size_t free_pages  = memory::g_pmm.free_frames();
        size_t used_pages  = total_pages - free_pages;

        send_str(conn, "\r\n[MEMORY TELEMETRY]\r\n  Total RAM   : ");
        send_dec(conn, (total_pages * 4096ULL) / (1024ULL * 1024ULL));
        send_str(conn, " MiB (");
        send_dec(conn, total_pages);
        send_str(conn, " frames)\r\n  Used RAM    : ");
        send_dec(conn, (used_pages * 4096ULL) / 1024ULL);
        send_str(conn, " KiB (");
        send_dec(conn, used_pages);
        send_str(conn, " frames)\r\n  Free RAM    : ");
        send_dec(conn, (free_pages * 4096ULL) / (1024ULL * 1024ULL));
        send_str(conn, " MiB (");
        send_dec(conn, free_pages);
        send_str(conn, " frames)\r\nllamaos# ");
    } else if (llamaos::strcmp(cmd, "ps") == 0) {
        size_t active_procs = userland::ProcessManager::active_process_count();
        send_str(conn, "\r\n[PROCESS LIST] Active Processes: ");
        send_dec(conn, active_procs);
        send_str(conn, "\r\n  PID  STATE    NAME\r\n  -------------------------\r\n");

        for (uint32_t pid = 1; pid <= 16; ++pid) {
            auto* p = userland::ProcessManager::get_process(pid);
            if (p && p->active) {
                send_str(conn, "  ");
                send_dec(conn, p->pid);
                send_str(conn, "    ");
                send_str(conn, p->state == userland::ProcessState::Running ? "RUNNING  " : "READY    ");
                send_str(conn, p->name);
                send_str(conn, "\r\n");
            }
        }
        send_str(conn, "llamaos# ");
    } else if (llamaos::strcmp(cmd, "storage") == 0) {
        size_t count = storage::StorageManager::device_count();
        send_str(conn, "\r\n[STORAGE DEVICES] Total Devices: ");
        send_dec(conn, count);
        send_str(conn, "\r\n");

        for (size_t i = 0; i < count; ++i) {
            auto* dev = storage::StorageManager::get_device(i);
            if (dev) {
                uint64_t cap_mib = (dev->total_sectors() * dev->sector_size()) / (1024 * 1024);
                send_str(conn, "  Device [");
                send_dec(conn, i);
                send_str(conn, "]: ");
                send_str(conn, dev->name());
                send_str(conn, " Capacity: ");
                send_dec(conn, cap_mib);
                send_str(conn, " MiB (");
                send_dec(conn, dev->total_sectors());
                send_str(conn, " sectors, ");
                send_str(conn, dev->is_read_only() ? "RO)\r\n" : "RW)\r\n");
            }
        }
        send_str(conn, "llamaos# ");
    } else if (llamaos::strcmp(cmd, "reboot") == 0) {
        send_str(conn, "\r\n[POWER] Initiating system reboot now...\r\n");
        core::PowerManager::reboot();
    } else if (llamaos::strcmp(cmd, "poweroff") == 0) {
        send_str(conn, "\r\n[POWER] Initiating system poweroff now...\r\n");
        core::PowerManager::poweroff();
    } else {
        send_str(conn, "\r\nUnknown command: '");
        send_str(conn, cmd);
        send_str(conn, "'. Type 'help' for command list.\r\nllamaos# ");
    }
}

} // namespace llamaos::net
