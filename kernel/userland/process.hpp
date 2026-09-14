#pragma once

#include "core/types.hpp"
#include "memory/memory_types.hpp"
#include "threading/scheduler.hpp"
#include "elf_loader.hpp"

// =============================================================================
// LlamaOS/A - Phase 7 Minimal Userland Process Subsystem
// =============================================================================
// Encapsulates a Ring 3 user process with its loaded virtual address space,
// user stack, execution thread, and lifecycle state.
// =============================================================================

namespace llamaos::syscall {
struct ProcessTelemetry;
}

namespace llamaos::userland {

using ProcessId = uint32_t;
inline constexpr ProcessId INVALID_PROCESS_ID = 0;
inline constexpr size_t    MAX_PROCESSES      = 16;
inline constexpr size_t    PROCESS_NAME_MAX   = 32;

enum class ProcessState : uint8_t {
    Unused,
    Created,
    Running,
    Terminated
};

struct Process {
    ProcessId                      pid{INVALID_PROCESS_ID};
    char                           name[PROCESS_NAME_MAX]{};
    ProcessState                   state{ProcessState::Unused};
    memory::VirtualAddress         entry_point{0};
    memory::VirtualAddress         stack_top{0};
    memory::PhysicalAddress        cr3{0};
    threading::ThreadControlBlock* thread{nullptr};
    int64_t                        exit_code{0};
    bool                           active{false};
};

class ProcessManager {
public:
    static void init();

    // Spawns a new user process from an ELF64 image in memory
    static Process* create_process(const char* name, const uint8_t* elf_data, size_t elf_size);

    // Spawns a new user process directly from a binary on VFS
    static Process* create_process_from_vfs(const char* path, const char* name = nullptr);

    // Inquiries
    static Process* current_process() noexcept;
    static Process* get_process(ProcessId pid) noexcept;
    static size_t   active_process_count() noexcept;
    static size_t   get_processes(syscall::ProcessTelemetry* out_list, size_t max_count) noexcept;

    // Process termination
    [[noreturn]] static void terminate_current_process(int64_t exit_code);

    // Diagnostics
    static void dump_processes();

private:
    static Process  s_processes[MAX_PROCESSES];
    static ProcessId s_next_pid;
    static bool     s_initialized;
};

} // namespace llamaos::userland
