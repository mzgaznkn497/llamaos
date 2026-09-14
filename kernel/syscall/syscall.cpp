#include "syscall.hpp"
#include "arch/x86_64/cpu/msr.hpp"
#include "arch/x86_64/cpu/timer.hpp"
#include "drivers/serial.hpp"
#include "drivers/console/console.hpp"
#include "drivers/ps2/keyboard.hpp"
#include "drivers/ps2/scancode.hpp"
#include "threading/scheduler.hpp"
#include "core/kprint.hpp"
#include "core/power.hpp"
#include "fs/vfs.hpp"
#include "memory/pmm.hpp"
#include "userland/user_memory.hpp"
#include "userland/process.hpp"
#include "net/net_interface.hpp"

// =============================================================================
// LlamaOS/A - Phase 6 & 7 System Call Subsystem Implementation
// =============================================================================

namespace llamaos::syscall {

bool     SyscallManager::s_initialized{false};
uint64_t SyscallManager::s_syscall_count{0};
uint64_t SyscallManager::s_error_count{0};

void SyscallManager::init() {
    using namespace arch::x86_64;

    // 1. Enable System Call Extensions (SCE, bit 0) in IA32_EFER MSR (0xC0000080)
    uint64_t efer = rdmsr(Msr::Efer);
    wrmsr(Msr::Efer, efer | EFER_SCE_BIT);

    // 2. Configure Segment Selectors in IA32_STAR MSR (0xC0000081)
    // Bits [47:32] = Kernel CS/SS base (0x0008)
    // Bits [63:48] = User CS/SS base   (0x0020)
    wrmsr(Msr::Star, SYSCALL_STAR_VALUE);

    // 3. Configure 64-bit Target RIP in IA32_LSTAR MSR (0xC0000082)
    uint64_t entry_addr = reinterpret_cast<uint64_t>(syscall_entry);
    wrmsr(Msr::Lstar, entry_addr);

    // 4. Configure RFLAGS Mask in IA32_SFMASK MSR (0xC0000084)
    // Masks IF, DF, TF, NT, AC on SYSCALL entry to guarantee atomic kernel transition
    wrmsr(Msr::Sfmask, SYSCALL_SFMASK_VALUE);

    s_initialized = true;
    s_syscall_count = 0;
    s_error_count = 0;

    klog_info("SyscallManager initialized:");
    klog_info("  IA32_EFER.SCE : Enabled (raw EFER: %p)", rdmsr(Msr::Efer));
    klog_info("  IA32_STAR     : %p (KernelCS=0x08, KernelSS=0x10, UserBase=0x20)", SYSCALL_STAR_VALUE);
    klog_info("  IA32_LSTAR    : %p (syscall_entry)", entry_addr);
    klog_info("  IA32_SFMASK   : %p (IF, DF, TF, NT, AC masked)", SYSCALL_SFMASK_VALUE);
}

bool SyscallManager::verify() {
    using namespace arch::x86_64;

    if (!s_initialized) {
        klog_error("SyscallManager::verify: Subsystem not initialized!");
        return false;
    }

    uint64_t efer = rdmsr(Msr::Efer);
    if ((efer & EFER_SCE_BIT) == 0) {
        klog_error("SyscallManager::verify: IA32_EFER.SCE bit is not set (%p)!", efer);
        return false;
    }

    uint64_t star = rdmsr(Msr::Star);
    if (star != SYSCALL_STAR_VALUE) {
        klog_error("SyscallManager::verify: IA32_STAR mismatch: expected %p, got %p!",
                   SYSCALL_STAR_VALUE, star);
        return false;
    }

    uint64_t lstar = rdmsr(Msr::Lstar);
    uint64_t expected_lstar = reinterpret_cast<uint64_t>(syscall_entry);
    if (lstar != expected_lstar) {
        klog_error("SyscallManager::verify: IA32_LSTAR mismatch: expected %p, got %p!",
                   expected_lstar, lstar);
        return false;
    }

    uint64_t sfmask = rdmsr(Msr::Sfmask);
    if (sfmask != SYSCALL_SFMASK_VALUE) {
        klog_error("SyscallManager::verify: IA32_SFMASK mismatch: expected %p, got %p!",
                   SYSCALL_SFMASK_VALUE, sfmask);
        return false;
    }

    return true;
}

int64_t SyscallManager::dispatch(SyscallFrame* frame) {
    if (!frame) {
        s_error_count++;
        return SYS_ERR_FAULT;
    }

    s_syscall_count++;

    // Security Gate: If caller executed from Ring 3, verify that return RIP (frame->rcx) and return RSP (frame->rsp)
    // are canonical lower-half user space addresses to protect SYSRETQ from privilege hijacking or #GP faults
    if (frame->rcx < 0x0000800000000000ULL) {
        if (!userland::UserMemoryValidator::is_user_address(frame->rcx) ||
            !userland::UserMemoryValidator::is_user_address(frame->rsp)) {
            klog_error("Syscall Security Violation: Non-canonical user RIP (%p) or RSP (%p) on syscall entry!",
                       frame->rcx, frame->rsp);
            s_error_count++;
            userland::ProcessManager::terminate_current_process(SYS_ERR_FAULT);
        }
    }

    switch (frame->rax) {
        case SysWriteDebug: {
            const char* buf = reinterpret_cast<const char*>(frame->rdi);
            size_t len = static_cast<size_t>(frame->rsi);

            if (!buf) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            if (len == 0 || len > SYSCALL_MAX_DEBUG_WRITE_LEN) {
                s_error_count++;
                return SYS_ERR_INVAL;
            }

            // If caller executed from Ring 3 (lower-half user virtual address in frame->rcx),
            // strictly validate user buffer range and page permissions before dereference.
            if (frame->rcx < 0x0000800000000000ULL) {
                if (!userland::UserMemoryValidator::validate_user_buffer(buf, len, false)) {
                    s_error_count++;
                    return SYS_ERR_FAULT;
                }
            }

            // Emit characters directly to serial COM1 for robust observability
            drivers::SerialPort::write(buf, len);
            return static_cast<int64_t>(len);
        }

        case SysYield: {
            // Cooperatively release CPU via existing Phase 5 scheduler
            threading::Scheduler::yield();
            return SYS_SUCCESS;
        }

        case SysGetPid: {
            // If current execution context belongs to a user process, return PID;
            // otherwise return the underlying kernel thread ID.
            auto* proc = userland::ProcessManager::current_process();
            if (proc) {
                return static_cast<int64_t>(proc->pid);
            }
            auto* current = threading::Scheduler::current_thread();
            return static_cast<int64_t>(current ? current->id : 0);
        }

        case SysGetTicks: {
            // Return system timer tick count
            return static_cast<int64_t>(arch::x86_64::Timer::ticks());
        }

        case SysExit: {
            // Terminate user process
            int64_t exit_code = static_cast<int64_t>(frame->rdi);
            userland::ProcessManager::terminate_current_process(exit_code);
            return SYS_SUCCESS; // Unreachable
        }

        case SysRead: {
            int fd = static_cast<int>(frame->rdi);
            char* buf = reinterpret_cast<char*>(frame->rsi);
            size_t count = static_cast<size_t>(frame->rdx);
            if (!buf) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }

            if (count > 0 && !userland::UserMemoryValidator::validate_write_buffer(buf, count)) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }

            if (fd == 0) {
                // Stdin: read from PS/2 keyboard or Serial UART
                if (count == 0) return 0;
                arch::x86_64::enable_interrupts();
                while (true) {
                    if (auto* netif = net::NetInterface::default_interface()) {
                        netif->poll();
                    }

                    char ch = 0;
                    drivers::KeyEvent ev{};
                    if (drivers::Keyboard::pop_event(&ev)) {
                        if (ev.action == drivers::KeyAction::Press) {
                            if (ev.key == drivers::KeyCode::Enter) {
                                ch = '\n';
                            } else if (ev.key == drivers::KeyCode::Backspace) {
                                ch = '\b';
                            } else {
                                ch = drivers::key_event_to_ascii(ev);
                            }
                        }
                    }
                    if (ch == 0 && drivers::SerialPort::has_rx()) {
                        ch = drivers::SerialPort::get_char();
                        if (ch == '\r') ch = '\n';
                    }
                    if (ch != 0) {
                        buf[0] = ch;
                        return 1;
                    }
                    arch::x86_64::enable_interrupts();
                    threading::Scheduler::yield();
                }
            }
            return fs::Vfs::read(fd, buf, count);
        }

        case SysWrite: {
            int fd = static_cast<int>(frame->rdi);
            const char* buf = reinterpret_cast<const char*>(frame->rsi);
            size_t count = static_cast<size_t>(frame->rdx);
            if (!buf) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }

            if (count > 0 && !userland::UserMemoryValidator::validate_read_buffer(buf, count)) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }

            if (fd == 1 || fd == 2) {
                drivers::Console::write(buf, count);
                return static_cast<int64_t>(count);
            }
            return fs::Vfs::write(fd, buf, count);
        }

        case SysOpen: {
            const char* path = reinterpret_cast<const char*>(frame->rdi);
            int flags = static_cast<int>(frame->rsi);
            if (!path) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            size_t path_len = 0;
            if (!userland::UserMemoryValidator::validate_string(path, 256, &path_len) || path_len == 0) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(fs::Vfs::open(path, flags));
        }

        case SysClose: {
            int fd = static_cast<int>(frame->rdi);
            return static_cast<int64_t>(fs::Vfs::close(fd));
        }

        case SysReaddir: {
            int fd = static_cast<int>(frame->rdi);
            uint32_t index = static_cast<uint32_t>(frame->rsi);
            auto* entry = reinterpret_cast<fs::DirEntry*>(frame->rdx);
            if (!entry) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            if (!userland::UserMemoryValidator::validate_object_write(entry)) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(fs::Vfs::readdir(fd, index, entry));
        }

        case SysStat: {
            const char* path = reinterpret_cast<const char*>(frame->rdi);
            auto* st = reinterpret_cast<fs::FileStat*>(frame->rsi);
            if (!path || !st) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            size_t path_len = 0;
            if (!userland::UserMemoryValidator::validate_string(path, 256, &path_len) || path_len == 0) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            if (!userland::UserMemoryValidator::validate_object_write(st)) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(fs::Vfs::stat(path, st));
        }

        case SysMkdir: {
            const char* path = reinterpret_cast<const char*>(frame->rdi);
            if (!path) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            size_t path_len = 0;
            if (!userland::UserMemoryValidator::validate_string(path, 256, &path_len) || path_len == 0) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(fs::Vfs::mkdir(path));
        }

        case SysUnlink: {
            const char* path = reinterpret_cast<const char*>(frame->rdi);
            if (!path) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            size_t path_len = 0;
            if (!userland::UserMemoryValidator::validate_string(path, 256, &path_len) || path_len == 0) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(fs::Vfs::unlink(path));
        }

        case SysReboot: {
            core::PowerManager::reboot();
            return SYS_SUCCESS;
        }

        case SysPoweroff: {
            core::PowerManager::poweroff();
            return SYS_SUCCESS;
        }

        case SysMemInfo: {
            auto* total = reinterpret_cast<uint64_t*>(frame->rdi);
            auto* free_f = reinterpret_cast<uint64_t*>(frame->rsi);
            if (!total && !free_f) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            if (total) {
                if (!userland::UserMemoryValidator::validate_object_write(total)) {
                    s_error_count++;
                    return SYS_ERR_FAULT;
                }
                *total = memory::g_pmm.total_frames();
            }
            if (free_f) {
                if (!userland::UserMemoryValidator::validate_object_write(free_f)) {
                    s_error_count++;
                    return SYS_ERR_FAULT;
                }
                *free_f = memory::g_pmm.free_frames();
            }
            return SYS_SUCCESS;
        }

        case SysPs: {
            auto* list = reinterpret_cast<ProcessTelemetry*>(frame->rdi);
            size_t max_count = static_cast<size_t>(frame->rsi);
            if (!list || max_count == 0) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            if (!userland::UserMemoryValidator::validate_write_buffer(list, max_count * sizeof(ProcessTelemetry))) {
                s_error_count++;
                return SYS_ERR_FAULT;
            }
            return static_cast<int64_t>(userland::ProcessManager::get_processes(list, max_count));
        }

        default: {
            // Unknown or unsupported system call number
            s_error_count++;
            return SYS_ERR_NOSYS;
        }
    }
}

} // namespace llamaos::syscall

// External C dispatch hook called from syscall_entry.asm
extern "C" int64_t syscall_dispatch(llamaos::syscall::SyscallFrame* frame) {
    return llamaos::syscall::SyscallManager::dispatch(frame);
}
