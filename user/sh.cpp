// =============================================================================
// LlamaOS/A - Freestanding Ring 3 Userland Shell
// =============================================================================
// Executes in user space with CPL=3.
// Interacts with the kernel strictly via Phase 6/7 System Call ABI.
// Provides interactive CLI with filesystem manipulation, process inspection,
// memory telemetry, and system control.
// =============================================================================

using size_t   = unsigned long;
using int64_t  = long long;
using uint64_t = unsigned long long;
using uint32_t = unsigned int;
using uint16_t = unsigned short;
using uint8_t  = unsigned char;

// Syscall numbers
inline constexpr uint64_t SYS_WRITE_DEBUG = 1;
inline constexpr uint64_t SYS_YIELD       = 2;
inline constexpr uint64_t SYS_GETPID      = 3;
inline constexpr uint64_t SYS_GET_TICKS   = 4;
inline constexpr uint64_t SYS_EXIT        = 5;
inline constexpr uint64_t SYS_READ        = 6;
inline constexpr uint64_t SYS_WRITE       = 7;
inline constexpr uint64_t SYS_OPEN        = 8;
inline constexpr uint64_t SYS_CLOSE       = 9;
inline constexpr uint64_t SYS_READDIR     = 10;
inline constexpr uint64_t SYS_STAT        = 11;
inline constexpr uint64_t SYS_MKDIR       = 12;
inline constexpr uint64_t SYS_UNLINK      = 13;
inline constexpr uint64_t SYS_REBOOT      = 14;
inline constexpr uint64_t SYS_POWEROFF    = 15;
inline constexpr uint64_t SYS_MEMINFO     = 16;
inline constexpr uint64_t SYS_PS          = 17;

struct [[gnu::packed]] ProcessTelemetry {
    uint32_t pid{0};
    uint32_t ppid{0};
    uint32_t state{0};
    uint32_t pad{0};
    char     name[32]{0};
    uint64_t cpu_ticks{0};
    uint64_t memory_bytes{0};
};

inline constexpr int O_RDONLY = 0x0001;
inline constexpr int O_WRONLY = 0x0002;
inline constexpr int O_RDWR   = 0x0003;
inline constexpr int O_CREAT  = 0x0040;
inline constexpr int O_TRUNC  = 0x0200;
inline constexpr int O_APPEND = 0x0400;

struct DirEntry {
    char     name[64];
    uint8_t  type;
    uint8_t  pad[7];
    uint64_t size;
    uint64_t inode;
};

struct FileStat {
    uint8_t  type;
    uint8_t  pad[7];
    uint64_t size;
    uint64_t inode;
    uint32_t block_size;
    uint32_t pad2;
    uint64_t blocks;
};

// -----------------------------------------------------------------------------
// Syscall Wrappers
// -----------------------------------------------------------------------------
static inline int64_t sys_write(int fd, const void* buf, size_t count) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_read(int fd, void* buf, size_t count) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READ), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_open(const char* path, int flags) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_OPEN), "D"(path), "S"(flags)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_close(int fd) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOSE), "D"(fd)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_readdir(int fd, uint32_t index, DirEntry* entry) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READDIR), "D"(fd), "S"(index), "d"(entry)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_stat(const char* path, FileStat* st) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_STAT), "D"(path), "S"(st)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_mkdir(const char* path) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_MKDIR), "D"(path)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_unlink(const char* path) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_UNLINK), "D"(path)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_get_ticks() {
    int64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(SYS_GET_TICKS) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_getpid() {
    int64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(SYS_GETPID) : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_meminfo(uint64_t* total, uint64_t* free_frames) {
    asm volatile("syscall" : : "a"(SYS_MEMINFO), "D"(total), "S"(free_frames) : "rcx", "r11", "memory");
}

static inline int64_t sys_ps(ProcessTelemetry* list, size_t max_count) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_PS), "D"(list), "S"(max_count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline void sys_yield() {
    asm volatile("syscall" : : "a"(SYS_YIELD) : "rcx", "r11", "memory");
}

static inline void sys_reboot() {
    asm volatile("syscall" : : "a"(SYS_REBOOT) : "rcx", "r11", "memory");
}

static inline void sys_poweroff() {
    asm volatile("syscall" : : "a"(SYS_POWEROFF) : "rcx", "r11", "memory");
}

static inline void sys_exit(int64_t code) {
    asm volatile("syscall" : : "a"(SYS_EXIT), "D"(code) : "rcx", "r11", "memory");
    while (true) {}
}

// -----------------------------------------------------------------------------
// String Utilities
// -----------------------------------------------------------------------------
static size_t strlen(const char* s) {
    size_t l = 0;
    while (s && s[l]) l++;
    return l;
}

static int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

[[maybe_unused]] static int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i] || a[i] == '\0') {
            return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        }
    }
    return 0;
}

static void print(const char* s) {
    if (!s) return;
    sys_write(1, s, strlen(s));
}

static void print_dec(uint64_t val) {
    if (val == 0) {
        print("0");
        return;
    }
    char buf[32];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < idx / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = tmp;
    }
    buf[idx] = '\0';
    print(buf);
}

// -----------------------------------------------------------------------------
// Command Handlers
// -----------------------------------------------------------------------------
static void cmd_help() {
    print("Available Shell Commands:\n");
    print("  help           - Show this help menu\n");
    print("  clear          - Clear screen\n");
    print("  echo [args...] - Print arguments\n");
    print("  ls [dir]       - List directory contents\n");
    print("  cat <file>     - Display file contents\n");
    print("  mkdir <dir>    - Create a new directory\n");
    print("  touch <file>   - Create an empty file\n");
    print("  rm <file>      - Delete a file\n");
    print("  ps             - Query process telemetry\n");
    print("  mem            - Display physical memory status\n");
    print("  uptime         - Display system uptime\n");
    print("  reboot         - Reboot system\n");
    print("  poweroff       - Shutdown system\n");
}

static void cmd_ls(const char* path) {
    const char* target = (path && *path) ? path : "/";
    int fd = static_cast<int>(sys_open(target, O_RDONLY));
    if (fd < 0) {
        print("ls: cannot access '");
        print(target);
        print("': No such directory\n");
        return;
    }

    print("Directory listing for '");
    print(target);
    print("':\n");

    DirEntry entry{};
    uint32_t idx = 0;
    while (sys_readdir(fd, idx++, &entry) == 0) {
        if (entry.name[0] == '\0') break;
        if (entry.type == 2) {
            print("  [DIR]  ");
        } else {
            print("  [FILE] ");
        }
        print(entry.name);
        if (entry.type != 2) {
            print(" (");
            print_dec(entry.size);
            print(" B)");
        }
        print("\n");
    }
    sys_close(fd);
}

static void cmd_cat(const char* path) {
    if (!path || !*path) {
        print("cat: missing file path\n");
        return;
    }

    int fd = static_cast<int>(sys_open(path, O_RDONLY));
    if (fd < 0) {
        print("cat: cannot open '");
        print(path);
        print("': No such file\n");
        return;
    }

    char buf[128];
    int64_t n;
    while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }
    print("\n");
    sys_close(fd);
}

static void cmd_mkdir(const char* path) {
    if (!path || !*path) {
        print("mkdir: missing directory name\n");
        return;
    }
    int64_t res = sys_mkdir(path);
    if (res != 0) {
        print("mkdir: failed to create directory '");
        print(path);
        print("'\n");
    }
}

static void cmd_touch(const char* path) {
    if (!path || !*path) {
        print("touch: missing file path\n");
        return;
    }
    int fd = static_cast<int>(sys_open(path, O_CREAT | O_WRONLY));
    if (fd < 0) {
        print("touch: cannot create file '");
        print(path);
        print("'\n");
        return;
    }
    sys_close(fd);
}

static void cmd_rm(const char* path) {
    if (!path || !*path) {
        print("rm: missing file path\n");
        return;
    }
    int64_t res = sys_unlink(path);
    if (res != 0) {
        print("rm: cannot remove '");
        print(path);
        print("'\n");
    }
}

static void cmd_mem() {
    uint64_t total = 0, free_frames = 0;
    sys_meminfo(&total, &free_frames);
    uint64_t used = total - free_frames;

    print("Physical Memory Status (PMM):\n");
    print("  Total Memory : "); print_dec((total * 4096ULL) / (1024ULL * 1024ULL)); print(" MiB ("); print_dec(total); print(" frames)\n");
    print("  Used Memory  : "); print_dec((used * 4096ULL) / 1024ULL); print(" KiB ("); print_dec(used); print(" frames)\n");
    print("  Free Memory  : "); print_dec((free_frames * 4096ULL) / (1024ULL * 1024ULL)); print(" MiB ("); print_dec(free_frames); print(" frames)\n");
}

static void cmd_ps() {
    ProcessTelemetry procs[16];
    int64_t count = sys_ps(procs, 16);
    print("Process Status (Active: ");
    if (count > 0) print_dec(static_cast<uint64_t>(count)); else print("0");
    print("):\n");
    print("  PID  STATE    NAME\n");
    print("  -------------------------\n");
    if (count <= 0) {
        int64_t pid = sys_getpid();
        print("  "); print_dec(static_cast<uint64_t>(pid)); print("    RUNNING  sh (current)\n");
        return;
    }
    for (int64_t i = 0; i < count; ++i) {
        print("  ");
        print_dec(procs[i].pid);
        print("    ");
        if (procs[i].state == 2) {
            print("RUNNING  ");
        } else if (procs[i].state == 1) {
            print("READY    ");
        } else {
            print("OTHER    ");
        }
        print(procs[i].name);
        print("\n");
    }
}

static void cmd_uptime() {
    int64_t ticks = sys_get_ticks();
    uint64_t sec = static_cast<uint64_t>(ticks) / 100;
    print("Uptime: "); print_dec(sec); print(" seconds ("); print_dec(static_cast<uint64_t>(ticks)); print(" ticks)\n");
}

// -----------------------------------------------------------------------------
// Shell Main Loop
// -----------------------------------------------------------------------------
extern "C" [[gnu::section(".text.entry")]] void _start() {
    print("\n");
    print("============================================================\n");
    print(" LlamaOS/A Interactive Userland Shell (/bin/sh)\n");
    print(" Running in Ring 3 (CPL=3) | POSIX VFS & Storage Online\n");
    print("============================================================\n");
    print("Type 'help' to view available commands.\n\n");

    char line[128];
    size_t line_len = 0;

    while (true) {
        print("llamaos$ ");
        line_len = 0;

        while (true) {
            char c = 0;
            int64_t r = sys_read(0, &c, 1);
            if (r <= 0) {
                sys_yield();
                continue;
            }

            if (c == '\n' || c == '\r') {
                print("\n");
                line[line_len] = '\0';
                break;
            } else if (c == '\b' || c == 0x7F) {
                if (line_len > 0) {
                    line_len--;
                    print("\b \b");
                }
            } else if (line_len < sizeof(line) - 1 && c >= ' ' && c <= '~') {
                line[line_len++] = c;
                char echo[2] = {c, '\0'};
                print(echo);
            }
        }

        // Parse command
        char* cmd = line;
        while (*cmd == ' ') cmd++;
        if (*cmd == '\0') continue;

        char* arg = cmd;
        while (*arg && *arg != ' ') arg++;
        if (*arg == ' ') {
            *arg++ = '\0';
            while (*arg == ' ') arg++;
        }

        if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "clear") == 0) {
            print("\033[2J\033[H");
        } else if (strcmp(cmd, "echo") == 0) {
            print(arg);
            print("\n");
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(arg);
        } else if (strcmp(cmd, "cat") == 0) {
            cmd_cat(arg);
        } else if (strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(arg);
        } else if (strcmp(cmd, "touch") == 0) {
            cmd_touch(arg);
        } else if (strcmp(cmd, "rm") == 0) {
            cmd_rm(arg);
        } else if (strcmp(cmd, "ps") == 0) {
            cmd_ps();
        } else if (strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (strcmp(cmd, "uptime") == 0) {
            cmd_uptime();
        } else if (strcmp(cmd, "reboot") == 0) {
            print("Rebooting system...\n");
            sys_reboot();
        } else if (strcmp(cmd, "poweroff") == 0) {
            print("Shutting down system...\n");
            sys_poweroff();
        } else if (strcmp(cmd, "exit") == 0) {
            print("Shell exiting...\n");
            sys_exit(0);
        } else {
            print("sh: command not found: ");
            print(cmd);
            print(". Type 'help' for available commands.\n");
        }
    }
}
