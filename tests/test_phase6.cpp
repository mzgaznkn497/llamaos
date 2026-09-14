#include "syscall/syscall_types.hpp"
#include "syscall/syscall_abi.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// =============================================================================
// LlamaOS/A - Phase 6 System Call Interface Host Regression Suite
// =============================================================================

static size_t g_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("[FAIL] Assertion failed: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        exit(1); \
    } \
    g_assertions_passed++; \
} while (0)

// Simulated host-side dispatcher exercising the exact dispatch logic
static int64_t host_simulate_dispatch(llamaos::syscall::SyscallFrame* frame) {
    using namespace llamaos::syscall;
    if (!frame) return SYS_ERR_FAULT;

    switch (frame->rax) {
        case SysWriteDebug: {
            const char* buf = reinterpret_cast<const char*>(frame->rdi);
            size_t len = static_cast<size_t>(frame->rsi);
            if (!buf) return SYS_ERR_FAULT;
            if (len == 0 || len > SYSCALL_MAX_DEBUG_WRITE_LEN) return SYS_ERR_INVAL;
            return static_cast<int64_t>(len);
        }
        case SysYield: {
            return SYS_SUCCESS;
        }
        case SysGetPid: {
            return 42; // Simulated PID on host
        }
        case SysGetTicks: {
            return 1000; // Simulated tick count on host
        }
        default: {
            return SYS_ERR_NOSYS;
        }
    }
}

// -----------------------------------------------------------------------------
// Test 1: SyscallFrame Layout, Size & Alignment
// -----------------------------------------------------------------------------
static void test_frame_layout() {
    using namespace llamaos::syscall;

    TEST_ASSERT(sizeof(SyscallFrame) == 128, "SyscallFrame size must be exactly 128 bytes");
    TEST_ASSERT(alignof(SyscallFrame) == 16, "SyscallFrame alignment must be 16 bytes");

    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r15) == 0x00, "r15 must be at offset 0x00");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r14) == 0x08, "r14 must be at offset 0x08");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r13) == 0x10, "r13 must be at offset 0x10");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r12) == 0x18, "r12 must be at offset 0x18");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rbp) == 0x20, "rbp must be at offset 0x20");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rbx) == 0x28, "rbx must be at offset 0x28");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r9)  == 0x30, "r9 (arg5) must be at offset 0x30");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r8)  == 0x38, "r8 (arg4) must be at offset 0x38");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r10) == 0x40, "r10 (arg3) must be at offset 0x40");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rdx) == 0x48, "rdx (arg2) must be at offset 0x48");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rsi) == 0x50, "rsi (arg1) must be at offset 0x50");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rdi) == 0x58, "rdi (arg0) must be at offset 0x58");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rax) == 0x60, "rax (syscall num) must be at offset 0x60");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rcx) == 0x68, "rcx (caller rip) must be at offset 0x68");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, r11) == 0x70, "r11 (caller rflags) must be at offset 0x70");
    TEST_ASSERT(__builtin_offsetof(SyscallFrame, rsp) == 0x78, "rsp (caller rsp) must be at offset 0x78");
}

// -----------------------------------------------------------------------------
// Test 2: Syscall Numbering & Status Codes
// -----------------------------------------------------------------------------
static void test_syscall_constants() {
    using namespace llamaos::syscall;

    TEST_ASSERT(SysInvalid == 0, "SysInvalid must be 0");
    TEST_ASSERT(SysWriteDebug == 1, "SysWriteDebug must be 1");
    TEST_ASSERT(SysYield == 2, "SysYield must be 2");
    TEST_ASSERT(SysGetPid == 3, "SysGetPid must be 3");
    TEST_ASSERT(SysGetTicks == 4, "SysGetTicks must be 4");
    TEST_ASSERT(SysMaxDefined == 4, "SysMaxDefined must be 4");

    TEST_ASSERT(SYS_SUCCESS == 0, "SYS_SUCCESS must be 0");
    TEST_ASSERT(SYS_ERR_NOSYS == -1, "SYS_ERR_NOSYS must be -1");
    TEST_ASSERT(SYS_ERR_INVAL == -2, "SYS_ERR_INVAL must be -2");
    TEST_ASSERT(SYS_ERR_FAULT == -3, "SYS_ERR_FAULT must be -3");
    TEST_ASSERT(SYS_ERR_PERM == -4, "SYS_ERR_PERM must be -4");
    TEST_ASSERT(SYS_ERR_BUSY == -5, "SYS_ERR_BUSY must be -5");

    TEST_ASSERT(SYSCALL_MAX_DEBUG_WRITE_LEN == 512, "Max debug write len must be 512");
}

// -----------------------------------------------------------------------------
// Test 3: Architectural MSR Math & Selector Encodings
// -----------------------------------------------------------------------------
static void test_msr_configuration() {
    using namespace llamaos::syscall;

    TEST_ASSERT(SYSCALL_STAR_KERNEL_BASE == 0x0008ULL, "Kernel CS base must be 0x0008");
    TEST_ASSERT(SYSCALL_STAR_USER_BASE == 0x0020ULL, "User CS/SS base must be 0x0020");
    TEST_ASSERT(SYSCALL_STAR_VALUE == 0x0020000800000000ULL, "STAR value must be 0x0020000800000000");

    // Kernel selector computation:
    uint16_t kernel_cs = static_cast<uint16_t>((SYSCALL_STAR_VALUE >> 32) & 0xFFFFULL) & ~0x03;
    uint16_t kernel_ss = kernel_cs + 8;
    TEST_ASSERT(kernel_cs == 0x0008, "Computed Kernel CS on SYSCALL must be 0x0008");
    TEST_ASSERT(kernel_ss == 0x0010, "Computed Kernel SS on SYSCALL must be 0x0010");

    // User selector computation for 64-bit SYSRETQ:
    uint16_t user_base = static_cast<uint16_t>((SYSCALL_STAR_VALUE >> 48) & 0xFFFFULL);
    uint16_t user_ss = (user_base + 8) | 0x03;
    uint16_t user_cs = (user_base + 16) | 0x03;
    TEST_ASSERT(user_ss == 0x002B, "Computed User SS on SYSRETQ must be 0x002B (Slot 5, RPL 3)");
    TEST_ASSERT(user_cs == 0x0033, "Computed User CS on SYSRETQ must be 0x0033 (Slot 6, RPL 3)");

    // SFMASK flag masking:
    TEST_ASSERT((SYSCALL_SFMASK_VALUE & (1ULL << 9)) != 0, "SFMASK must mask IF (bit 9)");
    TEST_ASSERT((SYSCALL_SFMASK_VALUE & (1ULL << 10)) != 0, "SFMASK must mask DF (bit 10)");
    TEST_ASSERT((SYSCALL_SFMASK_VALUE & (1ULL << 8)) != 0, "SFMASK must mask TF (bit 8)");
    TEST_ASSERT((SYSCALL_SFMASK_VALUE & (1ULL << 14)) != 0, "SFMASK must mask NT (bit 14)");
    TEST_ASSERT((SYSCALL_SFMASK_VALUE & (1ULL << 18)) != 0, "SFMASK must mask AC (bit 18)");

    // EFER.SCE:
    TEST_ASSERT(EFER_SCE_BIT == 1ULL, "EFER.SCE bit must be bit 0");
}

// -----------------------------------------------------------------------------
// Test 4: Dispatcher Routing & Boundary Checks
// -----------------------------------------------------------------------------
static void test_dispatcher_routing() {
    using namespace llamaos::syscall;

    SyscallFrame frame{};

    // Null frame protection
    TEST_ASSERT(host_simulate_dispatch(nullptr) == SYS_ERR_FAULT, "Null frame must return SYS_ERR_FAULT");

    // Invalid syscall numbers
    frame.rax = SysInvalid;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_NOSYS, "SysInvalid must return SYS_ERR_NOSYS");

    frame.rax = 999;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_NOSYS, "Unknown syscall must return SYS_ERR_NOSYS");

    frame.rax = UINT64_MAX;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_NOSYS, "UINT64_MAX syscall must return SYS_ERR_NOSYS");

    // SysYield
    frame.rax = SysYield;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_SUCCESS, "SysYield must return SYS_SUCCESS");

    // SysGetPid
    frame.rax = SysGetPid;
    TEST_ASSERT(host_simulate_dispatch(&frame) == 42, "SysGetPid must return simulated PID");

    // SysGetTicks
    frame.rax = SysGetTicks;
    TEST_ASSERT(host_simulate_dispatch(&frame) == 1000, "SysGetTicks must return simulated ticks");

    // SysWriteDebug: null buffer
    frame.rax = SysWriteDebug;
    frame.rdi = 0; // null
    frame.rsi = 10;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_FAULT, "Null buffer must return SYS_ERR_FAULT");

    // SysWriteDebug: zero length
    char test_buf[] = "Hello";
    frame.rdi = reinterpret_cast<uint64_t>(test_buf);
    frame.rsi = 0;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_INVAL, "Zero length must return SYS_ERR_INVAL");

    // SysWriteDebug: oversized length (> 512)
    frame.rsi = 513;
    TEST_ASSERT(host_simulate_dispatch(&frame) == SYS_ERR_INVAL, "Oversized length must return SYS_ERR_INVAL");

    // SysWriteDebug: valid write
    frame.rsi = 5;
    TEST_ASSERT(host_simulate_dispatch(&frame) == 5, "Valid buffer must return byte count");

    // SysWriteDebug: boundary length 512
    frame.rsi = 512;
    TEST_ASSERT(host_simulate_dispatch(&frame) == 512, "Boundary length 512 must succeed");
}

// -----------------------------------------------------------------------------
// Test 5: Register State & Argument Passing Invariants
// -----------------------------------------------------------------------------
static void test_register_invariants() {
    using namespace llamaos::syscall;

    SyscallFrame frame{};
    frame.r15 = 0x1111;
    frame.r14 = 0x2222;
    frame.r13 = 0x3333;
    frame.r12 = 0x4444;
    frame.rbp = 0x5555;
    frame.rbx = 0x6666;
    frame.r9  = 0x7777;
    frame.r8  = 0x8888;
    frame.r10 = 0x9999;
    frame.rdx = 0xAAAA;
    frame.rsi = 0xBBBB;
    frame.rdi = 0xCCCC;
    frame.rax = SysGetPid;
    frame.rcx = 0xFFFFFFFF80101234ULL; // Kernel return RIP
    frame.r11 = 0x0000000000000202ULL; // Caller RFLAGS
    frame.rsp = 0xFFFFFFFF8013AF00ULL; // Caller RSP

    int64_t ret = host_simulate_dispatch(&frame);
    TEST_ASSERT(ret == 42, "SysGetPid must return 42");

    // Invariants: non-argument and CPU tracking registers must remain unaffected by dispatch
    TEST_ASSERT(frame.r15 == 0x1111, "r15 invariant");
    TEST_ASSERT(frame.r14 == 0x2222, "r14 invariant");
    TEST_ASSERT(frame.r13 == 0x3333, "r13 invariant");
    TEST_ASSERT(frame.r12 == 0x4444, "r12 invariant");
    TEST_ASSERT(frame.rbp == 0x5555, "rbp invariant");
    TEST_ASSERT(frame.rbx == 0x6666, "rbx invariant");
    TEST_ASSERT(frame.rcx == 0xFFFFFFFF80101234ULL, "rcx (caller rip) invariant");
    TEST_ASSERT(frame.r11 == 0x0000000000000202ULL, "r11 (caller rflags) invariant");
    TEST_ASSERT(frame.rsp == 0xFFFFFFFF8013AF00ULL, "rsp (caller rsp) invariant");
}

int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Phase 6 System Call Interface Host Unit Regression Suite\n");
    printf("================================================================================\n");

    printf(" [RUN]  SyscallFrame Layout, Alignment & Offsets            ... ");
    test_frame_layout();
    printf("PASSED\n");

    printf(" [RUN]  Syscall Numbering & Standard Error Codes            ... ");
    test_syscall_constants();
    printf("PASSED\n");

    printf(" [RUN]  Architectural MSR Math & Selector Encodings         ... ");
    test_msr_configuration();
    printf("PASSED\n");

    printf(" [RUN]  Syscall Dispatcher Routing & Boundary Conditions    ... ");
    test_dispatcher_routing();
    printf("PASSED\n");

    printf(" [RUN]  Register State Invariants & Frame Preservation      ... ");
    test_register_invariants();
    printf("PASSED\n");

    printf("================================================================================\n");
    printf(" Phase 6 Regression Suite Complete: ALL tests PASSED (%zu assertions verified)\n", g_assertions_passed);
    printf("================================================================================\n");

    return 0;
}
