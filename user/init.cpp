// =============================================================================
// LlamaOS/A - Phase 7 Minimal Userland Init Program
// =============================================================================
// Freestanding Ring 3 user program executing with CPL=3.
// Interacts with the kernel strictly via Phase 6/7 System Call ABI.
// =============================================================================

using size_t  = unsigned long;
using int64_t = long long;
using uint64_t= unsigned long long;
using uint16_t= unsigned short;

// -----------------------------------------------------------------------------
// Minimal Freestanding Userland Syscall Wrappers
// -----------------------------------------------------------------------------
static inline int64_t sys_write_debug(const char* buf, size_t len) {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(1), "D"(buf), "S"(len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_yield() {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_getpid() {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t sys_get_ticks() {
    int64_t ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(4)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline void sys_exit(int64_t code) {
    asm volatile(
        "syscall"
        :
        : "a"(5), "D"(code)
        : "rcx", "r11", "memory"
    );
    while (true) {} // Unreachable
}

// -----------------------------------------------------------------------------
// Helper string printer
// -----------------------------------------------------------------------------
static inline void print(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    sys_write_debug(str, len);
}

// -----------------------------------------------------------------------------
// Entry Point: _start
// -----------------------------------------------------------------------------
extern "C" [[gnu::section(".text.entry")]] void _start() {
    // 1. Hardware Verification: Inspect CS selector to prove CPL=3
    uint16_t cs_val = 0;
    asm volatile("mov %%cs, %0" : "=r"(cs_val));

    // Bottom 2 bits of CS represent Current Privilege Level (CPL)
    if ((cs_val & 0x03) == 3) {
        print("[USER_R3_ENTERED] Running in Ring 3 user space with CPL=3.\n");
    } else {
        print("[USER_R3_FAIL] CPU is not running with CPL=3!\n");
        sys_exit(1);
    }

    // 2. Syscall verification: Basic write syscall from Ring 3
    int64_t write_ret = sys_write_debug("[USER_SYSCALL_OK] Basic system call invocation from Ring 3 confirmed.\n", 69);
    if (write_ret <= 0) {
        sys_exit(2);
    }

    // 3. Process identity verification: SYS_getpid
    int64_t pid = sys_getpid();
    if (pid >= 1) {
        print("[USER_GETPID_OK] Process ID query returned valid PID.\n");
    } else {
        print("[USER_GETPID_FAIL] Process ID query failed!\n");
        sys_exit(3);
    }

    // 4. System timer telemetry query: SYS_get_ticks
    int64_t ticks = sys_get_ticks();
    if (ticks >= 0) {
        print("[USER_TICKS_OK] System timer tick count query returned valid counter.\n");
    } else {
        print("[USER_TICKS_FAIL] System timer query failed!\n");
        sys_exit(4);
    }

    // 5. Preemptive multitasking verification:
    // Execute a pure user-mode compute loop across multiple timer ticks without voluntary yielding.
    // This forces PIT IRQ0 to preempt execution at CPL=3, save/restore state via iretq,
    // and survive back into Ring 3 execution.
    int64_t start_ticks = sys_get_ticks();
    volatile uint64_t compute_sink = 0;
    while ((sys_get_ticks() - start_ticks) < 4) {
        for (int j = 0; j < 50000; ++j) {
            compute_sink = compute_sink + j;
        }
    }
    print("[USER_PREEMPT_OK] Timer interrupts and user preemption survived.\n");

    // 6. Cooperative multitasking: Repeated SYS_yield calls from Ring 3
    for (int i = 0; i < 3; ++i) {
        int64_t yield_ret = sys_yield();
        if (yield_ret != 0) {
            print("[USER_YIELD_FAIL] Yield returned error!\n");
            sys_exit(5);
        }
    }
    print("[USER_YIELD_OK] Multiple cooperative SYS_yield cycles completed successfully.\n");

    // 6. Clean process termination: SYS_exit
    print("[USER_EXIT_OK] Userland program reached clean termination point.\n");
    sys_exit(0);
}
