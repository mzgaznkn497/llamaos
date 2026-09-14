#include "per_cpu.hpp"
#include "arch/x86_64/cpu/cpu.hpp"
#include "core/kprint.hpp"

// =============================================================================
// LlamaOS/A - Per-CPU State Implementation (Blocker 10)
// =============================================================================

namespace llamaos::arch::x86_64 {

CpuLocalData   PerCpuManager::s_cpus[MAX_CPUS]{};
size_t         PerCpuManager::s_online_cpus = 0;
sync::Spinlock PerCpuManager::s_lock;

void PerCpuManager::init_bsp() {
    sync::SpinlockGuard guard(s_lock);
    for (size_t i = 0; i < MAX_CPUS; ++i) {
        s_cpus[i] = CpuLocalData{};
        s_cpus[i].cpu_id = static_cast<uint32_t>(i);
    }

    // Initialize Bootstrap Processor (BSP, CPU 0)
    s_cpus[0].cpu_id = 0;
    s_cpus[0].apic_id = 0;
    s_cpus[0].is_bsp = true;
    s_cpus[0].online = true;
    s_online_cpus = 1;

    klog_info("PerCpuManager: Initialized BSP Core 0 (APIC ID 0)");
    klog_info("PerCpuManager: Production Capability Classification: [SINGLE-CPU ONLY]");
}

uint32_t PerCpuManager::current_cpu_id() noexcept {
    // In single-CPU / BSP-only mode, active CPU is always 0
    return 0;
}

CpuLocalData& PerCpuManager::current() noexcept {
    return s_cpus[0];
}

CpuLocalData& PerCpuManager::get_cpu(size_t index) noexcept {
    if (index >= MAX_CPUS) return s_cpus[0];
    return s_cpus[index];
}

void PerCpuManager::record_tick(uint32_t cpu_id) noexcept {
    if (cpu_id < MAX_CPUS) {
        s_cpus[cpu_id].scheduler_ticks++;
    }
}

void PerCpuManager::dump_topology() {
    klog_info("================================================================================");
    klog_info("CPU Topology & Multiprocessing Capability Report:");
    klog_info("  Architecture       : x86-64 (AMD64 Long Mode)");
    klog_info("  Online Cores       : %u / %u", static_cast<uint32_t>(s_online_cpus), static_cast<uint32_t>(MAX_CPUS));
    klog_info("  Bootstrap Core     : CPU 0 (Online, BSP)");
    klog_info("  Application Cores  : APs Quiescent (INIT/SIPI Deferred)");
    klog_info("  Capability Status  : SINGLE-CPU ONLY");
    klog_info("================================================================================");
}

} // namespace llamaos::arch::x86_64
