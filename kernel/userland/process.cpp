#include "process.hpp"
#include "arch/x86_64/cpu/gdt.hpp"
#include "arch/x86_64/cpu/timer.hpp"
#include "syscall/syscall_types.hpp"
#include "memory/vmm.hpp"
#include "core/kprint.hpp"
#include "core/string.hpp"

extern "C" void enter_ring3(uint64_t rip, uint64_t rsp);

// =============================================================================
// LlamaOS/A - Phase 7 Minimal Userland Process Subsystem Implementation
// =============================================================================

namespace llamaos::userland {

Process   ProcessManager::s_processes[MAX_PROCESSES]{};
ProcessId ProcessManager::s_next_pid{1};
bool      ProcessManager::s_initialized{false};

static void user_process_entry(void* argument) {
    auto* proc = static_cast<Process*>(argument);
    if (!proc) {
        klog_error("user_process_entry: null process pointer!");
        threading::Scheduler::exit();
    }

    proc->state = ProcessState::Running;

    if (!proc->cr3.is_null()) {
        memory::VirtualMemoryManager::reload_cr3(proc->cr3);
    }

    klog_info("[USER_PROCESS] Launching user process '%s' (PID %u) in Ring 3 at %p (RSP=%p)...",
              proc->name, proc->pid, proc->entry_point.as_ptr(), proc->stack_top.as_ptr());

    enter_ring3(proc->entry_point.value(), proc->stack_top.value() - 8);

    // enter_ring3 does not return; safety fallback
    threading::Scheduler::exit();
}

void ProcessManager::init() {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        s_processes[i] = Process{};
    }
    s_next_pid = 1;
    s_initialized = true;

    klog_info("ProcessManager initialized (Max Processes: %u).", static_cast<uint32_t>(MAX_PROCESSES));
}

Process* ProcessManager::create_process(const char* name, const uint8_t* elf_data, size_t elf_size) {
    if (!s_initialized) init();
    if (!elf_data || elf_size == 0) return nullptr;

    // Ensure GDT user descriptors (UserData 0x28, UserCode 0x30) are installed
    arch::x86_64::PermanentGdt::install_user_descriptors();

    // 1. Find free Process slot
    Process* slot = nullptr;
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (!s_processes[i].active) {
            slot = &s_processes[i];
            break;
        }
    }
    if (!slot) {
        klog_error("ProcessManager: Out of process slots!");
        return nullptr;
    }

    // 2. Allocate isolated per-process virtual address space (PML4)
    slot->cr3 = memory::g_vmm.create_user_address_space();
    if (slot->cr3.is_null()) {
        klog_error("ProcessManager: Failed to allocate address space (PML4)!");
        return nullptr;
    }

    // 3. Load ELF executable into this isolated address space
    ElfLoadResult load_res = ElfLoader::load(elf_data, elf_size, slot->cr3);
    if (load_res.status != ElfLoadStatus::Success) {
        klog_error("ProcessManager: Failed to load ELF for '%s': %s",
                   name ? name : "unnamed", to_string(load_res.status));
        memory::g_vmm.destroy_user_address_space(slot->cr3);
        slot->cr3 = memory::PhysicalAddress(0);
        return nullptr;
    }

    // 4. Initialize Process record
    slot->pid = s_next_pid++;
    if (name) {
        llamaos::strncpy(slot->name, name, sizeof(slot->name));
    } else {
        llamaos::strncpy(slot->name, "user_proc", sizeof(slot->name));
    }
    slot->state = ProcessState::Created;
    slot->entry_point = load_res.entry_point;
    slot->stack_top = load_res.stack_top;
    slot->exit_code = 0;
    slot->active = true;

    // 5. Create backing kernel thread in Scheduler with bound CR3
    slot->thread = threading::Scheduler::create_thread(
        slot->name,
        user_process_entry,
        slot,
        threading::ThreadPriority::Normal
    );

    if (!slot->thread) {
        klog_error("ProcessManager: Failed to create thread for process '%s'!", slot->name);
        memory::g_vmm.destroy_user_address_space(slot->cr3);
        slot->cr3 = memory::PhysicalAddress(0);
        slot->active = false;
        return nullptr;
    }
    slot->thread->cr3 = slot->cr3.value();

    klog_info("ProcessManager: Created user process '%s' (PID %u, Thread ID %u, CR3 %p)",
              slot->name, slot->pid, slot->thread->id, reinterpret_cast<void*>(slot->cr3.value()));

    return slot;
}

Process* ProcessManager::create_process_from_vfs(const char* path, const char* name) {
    if (!s_initialized) init();
    if (!path) return nullptr;

    arch::x86_64::PermanentGdt::install_user_descriptors();

    Process* slot = nullptr;
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (!s_processes[i].active) {
            slot = &s_processes[i];
            break;
        }
    }
    if (!slot) {
        klog_error("ProcessManager: Out of process slots!");
        return nullptr;
    }

    slot->cr3 = memory::g_vmm.create_user_address_space();
    if (slot->cr3.is_null()) {
        klog_error("ProcessManager: Failed to allocate address space (PML4) for '%s'!", path);
        return nullptr;
    }

    ElfLoadResult load_res = ElfLoader::load_from_vfs(path, slot->cr3);
    if (load_res.status != ElfLoadStatus::Success) {
        klog_error("ProcessManager: Failed to load ELF from VFS '%s': %s",
                   path, to_string(load_res.status));
        memory::g_vmm.destroy_user_address_space(slot->cr3);
        slot->cr3 = memory::PhysicalAddress(0);
        return nullptr;
    }

    slot->pid = s_next_pid++;
    if (name) {
        llamaos::strncpy(slot->name, name, sizeof(slot->name));
    } else {
        llamaos::strncpy(slot->name, path, sizeof(slot->name));
    }
    slot->state = ProcessState::Created;
    slot->entry_point = load_res.entry_point;
    slot->stack_top = load_res.stack_top;
    slot->exit_code = 0;
    slot->active = true;

    slot->thread = threading::Scheduler::create_thread(
        slot->name,
        user_process_entry,
        slot,
        threading::ThreadPriority::Normal
    );

    if (!slot->thread) {
        klog_error("ProcessManager: Failed to create thread for process '%s'!", slot->name);
        memory::g_vmm.destroy_user_address_space(slot->cr3);
        slot->cr3 = memory::PhysicalAddress(0);
        slot->active = false;
        return nullptr;
    }
    slot->thread->cr3 = slot->cr3.value();

    klog_info("ProcessManager: Created user process from VFS '%s' (PID %u, Thread ID %u, CR3 %p)",
              slot->name, slot->pid, slot->thread->id, reinterpret_cast<void*>(slot->cr3.value()));

    return slot;
}

Process* ProcessManager::current_process() noexcept {
    auto* cur_thread = threading::Scheduler::current_thread();
    if (!cur_thread) return nullptr;

    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (s_processes[i].active && s_processes[i].thread == cur_thread) {
            return &s_processes[i];
        }
    }
    return nullptr;
}

Process* ProcessManager::get_process(ProcessId pid) noexcept {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (s_processes[i].active && s_processes[i].pid == pid) {
            return &s_processes[i];
        }
    }
    return nullptr;
}

size_t ProcessManager::active_process_count() noexcept {
    size_t count = 0;
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (s_processes[i].active) count++;
    }
    return count;
}

[[noreturn]] void ProcessManager::terminate_current_process(int64_t exit_code) {
    Process* proc = current_process();
    if (proc) {
        proc->state = ProcessState::Terminated;
        proc->exit_code = exit_code;
        proc->active = false;
        klog_info("[USER_PROCESS_EXIT] Process '%s' (PID %u) terminated with exit code %lld",
                  proc->name, proc->pid, exit_code);
        if (!proc->cr3.is_null()) {
            memory::VirtualMemoryManager::reload_cr3(memory::g_vmm.root_pml4_address());
            memory::g_vmm.destroy_user_address_space(proc->cr3);
            proc->cr3 = memory::PhysicalAddress(0);
        }
    }
    threading::Scheduler::exit();
}

void ProcessManager::dump_processes() {
    klog_info("--- ProcessManager Dump (Active: %u) ---", static_cast<uint32_t>(active_process_count()));
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (s_processes[i].active) {
            klog_info("  [PID %u] '%s' Entry=%p Stack=%p ThreadID=%u",
                      s_processes[i].pid,
                      s_processes[i].name,
                      s_processes[i].entry_point.as_ptr(),
                      s_processes[i].stack_top.as_ptr(),
                      s_processes[i].thread ? s_processes[i].thread->id : 0);
        }
    }
}

size_t ProcessManager::get_processes(syscall::ProcessTelemetry* out_list, size_t max_count) noexcept {
    if (!out_list || max_count == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < MAX_PROCESSES && count < max_count; ++i) {
        if (s_processes[i].active) {
            auto& dst = out_list[count++];
            dst.pid = s_processes[i].pid;
            dst.ppid = 0;
            dst.state = static_cast<uint32_t>(s_processes[i].state);
            dst.pad = 0;
            llamaos::strncpy(dst.name, s_processes[i].name, sizeof(dst.name) - 1);
            dst.name[sizeof(dst.name) - 1] = '\0';
            dst.cpu_ticks = arch::x86_64::Timer::ticks();
            dst.memory_bytes = 16 * 1024;
        }
    }
    return count;
}

} // namespace llamaos::userland
